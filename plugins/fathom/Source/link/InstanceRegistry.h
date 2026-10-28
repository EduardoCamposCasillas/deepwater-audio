#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

// Fase 14, sin JUCE (se prueba en tests/EngineTests.cpp): la lista de las instancias de Fathom que están vivas en este
// proceso, para que cada una pueda ver (y editar) a las demás.
//
// Por qué funciona: FL Studio carga cada plugin UNA vez por proceso (el .vst3 es una DLL) y todas sus instancias viven
// en ese proceso, así que comparten las variables estáticas de la DLL. Una lista estática es un punto de encuentro sin
// red ni archivos. Si FL corre el plugin en otro proceso ("bridged" o "Make bridged" en las opciones del wrapper), esa
// instancia tiene su propia lista y no ve a las demás.
//
// Hilos: las instancias se crean, se destruyen y se miran desde el hilo de mensajes; la lista igual tiene un mutex
// (algunos hosts crean plugins desde otro hilo al escanear). El audio NUNCA toca la lista: solo lee 'isWatched' (un
// atómico) para saber si alguien está mirando y vale la pena copiar sus muestras para el analizador.
namespace fathom::link
{

template <typename Member>
class InstanceRegistry
{
public:
    // La lista del proceso (una por tipo de miembro: los tests usan la suya).
    static InstanceRegistry& shared()
    {
        static InstanceRegistry registry;
        return registry;
    }

    // Se anota un miembro y recibe su número. Los números no se reutilizan: un número viejo nunca apunta a otro miembro.
    int add (Member& member)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        const int id = ++lastId;
        entries.push_back ({ id, &member });
        return id;
    }

    // Se borra un miembro. Los oyentes se enteran ANTES de que este método termine: el miembro llama a remove al
    // principio de su destructor, así que mientras los oyentes reaccionan (cerrar una edición a medias sobre él) sigue
    // entero.
    void remove (int id)
    {
        std::vector<std::function<void (int)>> toNotify;
        {
            const std::lock_guard<std::mutex> lock (mutex);
            entries.erase (std::remove_if (entries.begin(), entries.end(), [id] (const Entry& e) { return e.id == id; }), entries.end());
            for (const auto& listener : listeners)
                toNotify.push_back (listener.callback);
        }
        for (const auto& callback : toNotify) // fuera del lock: un oyente puede consultar la lista
            callback (id);
    }

    // El miembro con ese número, o nullptr si ya no existe. El puntero vale hasta que alguien llame a remove (en el
    // hilo de mensajes): no guardarlo de un cuadro al siguiente, buscarlo de nuevo.
    Member* find (int id) const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (const auto& e : entries)
            if (e.id == id)
                return e.member;
        return nullptr;
    }

    // Los números de todos, en el orden en que se crearon.
    std::vector<int> ids() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        std::vector<int> result;
        for (const auto& e : entries)
            result.push_back (e.id);
        return result;
    }

    // Avisos de que un miembro se va. Devuelve una ficha para quitar el aviso.
    int addListener (std::function<void (int)> callback)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        listeners.push_back ({ ++lastListener, std::move (callback) });
        return lastListener;
    }

    void removeListener (int token)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        listeners.erase (std::remove_if (listeners.begin(), listeners.end(), [token] (const Listener& l) { return l.token == token; }),
                         listeners.end());
    }

    // Cuántos están mirando a los demás. Mientras sea > 0 todos copian su salida a las tomas del analizador aunque su
    // propia ventana esté cerrada (sin nadie mirando no gastan nada en eso).
    void startWatching() noexcept { watchers.fetch_add (1, std::memory_order_relaxed); }
    void stopWatching() noexcept { watchers.fetch_sub (1, std::memory_order_relaxed); }
    bool isWatched() const noexcept { return watchers.load (std::memory_order_relaxed) > 0; } // también desde el audio

private:
    struct Entry
    {
        int id;
        Member* member;
    };
    struct Listener
    {
        int token;
        std::function<void (int)> callback;
    };

    mutable std::mutex mutex;
    std::vector<Entry> entries;
    std::vector<Listener> listeners;
    int lastId = 0, lastListener = 0;
    std::atomic<int> watchers { 0 };
};

// Lo que una instancia decidió ver de las demás (vive en su procesador y lo usa solo la GUI). No se guarda en el
// proyecto: los números cambian cada vez que FL abre el proyecto.
struct LinkSettings
{
    std::vector<int> shown; // las otras instancias cuyo espectro se ve
    int editable = -1;      // la que además muestra sus nodos para editarla desde aquí (−1 = ninguna)
    bool collisions = true; // marcar dónde choca esta instancia con las que se ven
    int sensitivityIndex = 1;

    bool isShown (int id) const noexcept { return std::find (shown.begin(), shown.end(), id) != shown.end(); }

    void setShown (int id, bool show)
    {
        if (show && ! isShown (id))
            shown.push_back (id);
        if (! show)
        {
            shown.erase (std::remove (shown.begin(), shown.end(), id), shown.end());
            if (editable == id)
                editable = -1;
        }
    }

    // Editar una implica verla.
    void setEditable (int id)
    {
        editable = id;
        if (id >= 0)
            setShown (id, true);
    }

    // Una instancia que ya no existe.
    void forget (int id) { setShown (id, false); }
};

} // namespace fathom::link
