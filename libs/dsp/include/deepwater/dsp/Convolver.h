#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <vector>

#include "deepwater/dsp/RealFft.h"

namespace deepwater::dsp
{

// --- Convolución por FFT particionada (nació en Fathom, Fase 10: el EQ de fase lineal; la reverb la va a reutilizar) --
//
// El problema. Un filtro FIR (una respuesta al impulso h de N muestras) se aplica con la convolución
// y[n] = Σ h[k]·x[n−k]: N multiplicaciones por muestra. Con N = 16384 (un EQ de fase lineal de calidad alta) son 16384
// por muestra y por canal: imposible en tiempo real. En frecuencia la convolución es un producto (Y = H·X), y la FFT
// cuesta N·log N: hacerla por bloques es cientos de veces más barato.
//
// Pero una sola FFT de todo el filtro obliga a esperar N muestras antes de tener salida (la latencia sería el largo
// del filtro). La solución estándar (Stockham 1966; "uniformly partitioned overlap-save", ver p. ej. F. Wefers,
// "Partitioned convolution algorithms for real-time auralization", 2015): partir h en K trozos de P muestras. Cada
// bloque de P muestras de entrada se transforma UNA vez (FFT de 2P) y se guarda en una "línea de retardo en
// frecuencia" (FDL) con los K espectros más recientes. La salida del bloque es
//     Y = Σ_p H_p · X_(n−p)        (el trozo p del filtro por la entrada de hace p bloques)
// y una FFT inversa de 2P. Overlap-save: la FFT ve los últimos 2P de entrada y de su inversa solo valen las últimas P
// muestras (las primeras están "contaminadas" por la convolución circular). Latencia: P muestras (hay que juntar un
// bloque), no N. Costo por muestra: 3 FFT de 2P cada P muestras + K productos complejos por bin.
//
// Varios caminos: el convolucionador tiene varias entradas y salidas y el kernel dice qué entrada va a qué salida con
// qué filtro (en estéreo con bandas mid/side hacen falta cuatro: L→L, R→L, L→R, R→R). Las entradas se transforman una
// vez y las comparten todos los caminos; cada salida es la suma de los suyos.
//
// Cambiar de filtro sin clic: con setKernel(k, true) el bloque siguiente se calcula con el filtro viejo Y con el nuevo
// (sobre la misma FDL: la entrada pasada es la misma) y se funde de uno al otro a lo largo de las P muestras del bloque.
// Es como interpolar los dos filtros en el tiempo; un EQ que se mueve cambia de kernel en cada bloque.
//
// Repartir el trabajo (Fathom, Fase 16). Con bloques del host más chicos que P, un callback de cada P/bloque hacía todo
// el cálculo: con 64 trozos y 4 caminos, varias veces el tiempo de un bloque de 32 muestras (el host corta aunque el
// promedio sea bajo). Pero en Σ H_p · X_(n−p) los trozos con p ≥ 1 multiplican entradas PASADAS, que ya están en la
// FDL mientras se junta el bloque actual: esa parte de la suma se va haciendo en cada llamada a process(), en
// proporción a las muestras que llegaron. En el borde quedan la FFT de la entrada nueva, el trozo p = 0 y la inversa.
// Misma latencia y el mismo resultado (salvo el orden de las sumas en float: el trozo 0 se suma al final).

// Un filtro listo para convolucionar: sus trozos ya transformados. Se rellena FUERA del hilo de audio (o en un diseño
// offline) y el convolucionador solo lo lee. No se puede tocar mientras un convolucionador lo esté usando.
class ConvolutionKernel
{
public:
    static constexpr int maxPaths = 8;

    // Un camino: de qué entrada a qué salida, y qué trozos tiene (los anteriores y posteriores son ceros: no se
    // calculan; un retardo puro tiene un solo trozo).
    struct Path
    {
        int input = 0, output = 0;
        int first = 0, count = 0;
    };

    // Fuera del hilo de audio: reserva sitio para 'maxPathCount' caminos de hasta maxPartitions·partitionSize muestras.
    void prepare (int newPartitionSize, int newMaxPartitions, int maxPathCount)
    {
        assert (maxPathCount <= maxPaths);
        partitionSize = newPartitionSize;
        bins = partitionSize + 1;
        maxPartitions = newMaxPartitions;
        pathCapacity = maxPathCount;
        const auto total = static_cast<size_t> (pathCapacity * maxPartitions * bins);
        re.assign (total, 0.0f);
        im.assign (total, 0.0f);
        fft.prepare (2 * partitionSize);
        scratch.assign (static_cast<size_t> (2 * partitionSize), 0.0f);
        clear();
    }

    void clear() noexcept { numPaths = 0; }

    // Agrega un camino con la respuesta al impulso 'ir' (de 'length' muestras; lo que pase del máximo se ignora). No
    // reserva memoria. Devuelve false si ya no hay sitio para otro camino.
    bool addPath (int input, int output, const float* ir, int length) noexcept
    {
        if (numPaths >= pathCapacity)
            return false;
        length = std::min (length, maxPartitions * partitionSize);
        Path path { input, output, 0, 0 };
        const int partitions = (length + partitionSize - 1) / partitionSize;
        int last = -1;
        for (int p = 0; p < partitions; ++p)
        {
            const int start = p * partitionSize, n = std::min (partitionSize, length - start);
            bool silent = true;
            for (int i = 0; i < n; ++i)
                silent = silent && ir[start + i] == 0.0f;
            if (silent)
                continue;
            if (last < 0)
                path.first = p;
            last = p;
        }
        if (last >= 0)
        {
            path.count = last - path.first + 1;
            for (int p = 0; p < path.count; ++p)
            {
                const int start = (path.first + p) * partitionSize, n = std::min (partitionSize, length - start);
                std::fill (scratch.begin(), scratch.end(), 0.0f);
                std::copy_n (ir + start, n, scratch.begin());
                fft.forward (scratch.data(), partitionRe (numPaths, p), partitionIm (numPaths, p));
            }
        }
        paths[static_cast<size_t> (numPaths++)] = path;
        return true;
    }

    [[nodiscard]] int getNumPaths() const noexcept { return numPaths; }
    [[nodiscard]] const Path& getPath (int index) const noexcept { return paths[static_cast<size_t> (index)]; }
    [[nodiscard]] int getPartitionSize() const noexcept { return partitionSize; }

    // El espectro del trozo p (contando desde Path::first) del camino 'path': partitionSize + 1 bins.
    [[nodiscard]] const float* partitionRe (int path, int p) const noexcept { return re.data() + offset (path, p); }
    [[nodiscard]] const float* partitionIm (int path, int p) const noexcept { return im.data() + offset (path, p); }

private:
    [[nodiscard]] size_t offset (int path, int p) const noexcept { return static_cast<size_t> ((path * maxPartitions + p) * bins); }
    float* partitionRe (int path, int p) noexcept { return re.data() + offset (path, p); }
    float* partitionIm (int path, int p) noexcept { return im.data() + offset (path, p); }

    int partitionSize = 0, bins = 0, maxPartitions = 0, pathCapacity = 0;
    std::vector<float> re, im; // [camino][trozo][bin]
    std::array<Path, maxPaths> paths {};
    int numPaths = 0;
    RealFft<float> fft;
    std::vector<float> scratch;
};

// Convolución en tiempo real con un ConvolutionKernel (ver arriba). Latencia: getLatency() muestras.
class PartitionedConvolver
{
public:
    static constexpr int maxInputs = 4;
    static constexpr int maxOutputs = 4;

    // Fuera del hilo de audio. Los kernels que se le pasen tienen que tener este mismo partitionSize y a lo sumo
    // maxPartitions trozos por camino.
    void prepare (int newPartitionSize, int newMaxPartitions, int newNumInputs, int newNumOutputs)
    {
        assert (newNumInputs <= maxInputs && newNumOutputs <= maxOutputs);
        partitionSize = newPartitionSize;
        bins = partitionSize + 1;
        maxPartitions = newMaxPartitions;
        numInputs = newNumInputs;
        numOutputs = newNumOutputs;
        fft.prepare (2 * partitionSize);
        for (int i = 0; i < numInputs; ++i)
        {
            auto& input = inputs[static_cast<size_t> (i)];
            input.history.assign (static_cast<size_t> (2 * partitionSize), 0.0f);
            input.re.assign (static_cast<size_t> (maxPartitions * bins), 0.0f);
            input.im.assign (input.re.size(), 0.0f);
        }
        for (int o = 0; o < numOutputs; ++o)
        {
            auto& output = outputs[static_cast<size_t> (o)];
            output.fifo.assign (static_cast<size_t> (partitionSize), 0.0f);
            for (auto* v : { &output.re, &output.im, &output.otherRe, &output.otherIm })
                v->assign (static_cast<size_t> (bins), 0.0f);
        }
        block.assign (static_cast<size_t> (2 * partitionSize), 0.0f);
        otherBlock.assign (block.size(), 0.0f);
        current = pending = nullptr;
        reset();
    }

    // Silencio en la entrada pasada y en la salida pendiente (el kernel se conserva).
    void reset() noexcept
    {
        for (int i = 0; i < numInputs; ++i)
        {
            auto& input = inputs[static_cast<size_t> (i)];
            std::fill (input.history.begin(), input.history.end(), 0.0f);
            std::fill (input.re.begin(), input.re.end(), 0.0f);
            std::fill (input.im.begin(), input.im.end(), 0.0f);
        }
        for (int o = 0; o < numOutputs; ++o)
            std::fill (outputs[static_cast<size_t> (o)].fifo.begin(), outputs[static_cast<size_t> (o)].fifo.end(), 0.0f);
        position = 0;
        head = 0;
        audible = nullptr;
        audibleFade = false;
        restartAhead();
    }

    // Kernel nuevo. Con 'crossfade' (y si ya había uno), el bloque siguiente se funde del viejo al nuevo; si no, el
    // nuevo reemplaza al viejo desde el bloque siguiente. Solo puede haber uno pendiente: si ya hay, devuelve false.
    // nullptr = sin filtro (las salidas dan silencio).
    bool setKernel (const ConvolutionKernel* kernel, bool crossfade) noexcept
    {
        if (pending != nullptr)
            return false;
        if (crossfade && current != nullptr && kernel != nullptr)
            pending = kernel;
        else
            current = kernel;
        return true;
    }

    // Suelta los dos kernels (el que suena y el pendiente): las salidas dan silencio hasta el próximo setKernel.
    void releaseKernels() noexcept { current = pending = audible = nullptr; }

    [[nodiscard]] bool hasPendingKernel() const noexcept { return pending != nullptr; }
    // El que suena y el que está esperando (el dueño de los kernels sabe así cuáles puede reutilizar).
    [[nodiscard]] const ConvolutionKernel* getCurrentKernel() const noexcept { return current; }
    [[nodiscard]] const ConvolutionKernel* getPendingKernel() const noexcept { return pending; }
    // El kernel de lo que está SALIENDO ahora (la salida va un bloque atrás del cálculo) y si ese bloque es un fundido.
    [[nodiscard]] const ConvolutionKernel* getAudibleKernel() const noexcept { return audible; }
    [[nodiscard]] bool isAudibleFade() const noexcept { return audibleFade; }

    [[nodiscard]] int getLatency() const noexcept { return partitionSize; }

    // 'in': numInputs canales; 'out': numOutputs canales (pueden ser los mismos buffers que 'in').
    void process (const float* const* in, float* const* out, int numSamples) noexcept
    {
        for (int done = 0; done < numSamples;)
        {
            const int count = std::min (numSamples - done, partitionSize - position);
            for (int i = 0; i < numInputs; ++i)
                std::copy_n (in[i] + done, count, inputs[static_cast<size_t> (i)].history.begin() + partitionSize + position);
            for (int o = 0; o < numOutputs; ++o)
                std::copy_n (outputs[static_cast<size_t> (o)].fifo.begin() + position, count, out[o] + done);
            position += count;
            done += count;
            if (position == partitionSize)
            {
                computeBlock();
                position = 0;
            }
            else
                workAhead();
        }
    }

private:
    struct Input
    {
        std::vector<float> history; // las últimas 2P muestras (la mitad de atrás = el bloque nuevo)
        std::vector<float> re, im;  // la FDL: los espectros de los últimos maxPartitions bloques
    };
    struct Output
    {
        std::vector<float> fifo;             // las P muestras que van saliendo
        std::vector<float> re, im;           // el espectro que se acumula para el bloque
        std::vector<float> otherRe, otherIm; // el del kernel pendiente (durante un fundido)
        bool used = false, otherUsed = false;
    };

    // Lo adelantado de un kernel para el bloque siguiente: de cuál es y hasta qué trozo llegó (camino k, trozo p; solo
    // los de edad ≥ 1).
    struct Ahead
    {
        const ConvolutionKernel* kernel = nullptr;
        int path = 0, part = 0, done = 0;
        bool finished = false;
    };

    void restartAhead() noexcept
    {
        for (int o = 0; o < numOutputs; ++o)
        {
            auto& output = outputs[static_cast<size_t> (o)];
            output.used = output.otherUsed = false;
        }
        ahead = { Ahead { current }, Ahead { pending } };
    }

    // Trozos de edad ≥ 1 de un kernel (lo que se puede adelantar).
    [[nodiscard]] int aheadUnits (const ConvolutionKernel* kernel) const noexcept
    {
        int units = 0;
        if (kernel != nullptr)
            for (int k = 0; k < kernel->getNumPaths(); ++k)
            {
                const auto& path = kernel->getPath (k);
                if (path.input < numInputs && path.output < numOutputs)
                    units += std::max (0, std::min (path.first + path.count, maxPartitions) - std::max (path.first, 1));
            }
        return units;
    }

    // Avanza lo adelantado de 'which' (0 = el kernel que suena, 1 = el pendiente) hasta 'target' trozos (o todos).
    void advanceAhead (int which, int target) noexcept
    {
        auto& state = ahead[static_cast<size_t> (which)];
        const auto* kernel = which == 0 ? current : pending;
        if (state.kernel != kernel)
        {
            // Cambió el kernel a mitad del bloque (setKernel sin fundido o un pendiente nuevo): lo de antes no sirve.
            for (int o = 0; o < numOutputs; ++o)
                (which == 0 ? outputs[static_cast<size_t> (o)].used : outputs[static_cast<size_t> (o)].otherUsed) = false;
            state = Ahead { kernel };
        }
        if (kernel == nullptr)
            return;
        const int nextHead = (head + 1) % maxPartitions;
        while (! state.finished && state.done < target)
        {
            if (state.path >= kernel->getNumPaths())
            {
                state.finished = true;
                break;
            }
            const auto& path = kernel->getPath (state.path);
            const int age = path.first + state.part;
            if (path.input >= numInputs || path.output >= numOutputs || state.part >= path.count || age >= maxPartitions)
            {
                ++state.path;
                state.part = 0;
                continue;
            }
            if (age >= 1)
            {
                accumulatePart (*kernel, which == 1, state.path, state.part, nextHead);
                ++state.done;
            }
            ++state.part;
        }
    }

    // Durante el bloque: lo adelantado en proporción a las muestras que ya llegaron.
    void workAhead() noexcept
    {
        for (int which = 0; which < 2; ++which)
        {
            const auto* kernel = which == 0 ? current : pending;
            const int units = aheadUnits (kernel);
            advanceAhead (which, (units * position + partitionSize - 1) / partitionSize);
        }
    }

    void computeBlock() noexcept
    {
        // 1) Lo que falte de los trozos de edad ≥ 1 (que solo leen entradas pasadas).
        advanceAhead (0, std::numeric_limits<int>::max());
        advanceAhead (1, std::numeric_limits<int>::max());

        // 2) La entrada nueva a frecuencia, a la FDL.
        head = (head + 1) % maxPartitions;
        for (int i = 0; i < numInputs; ++i)
        {
            auto& input = inputs[static_cast<size_t> (i)];
            const auto at = static_cast<size_t> (head * bins);
            fft.forward (input.history.data(), input.re.data() + at, input.im.data() + at);
            std::copy_n (input.history.begin() + partitionSize, partitionSize, input.history.begin()); // el bloque nuevo pasa a ser el viejo
        }

        // 3) Los trozos de edad 0 (la entrada recién llegada), por salida (con los dos kernels si hay fundido).
        for (int which = 0; which < 2; ++which)
            if (const auto* kernel = which == 0 ? current : pending)
                for (int k = 0; k < kernel->getNumPaths(); ++k)
                {
                    const auto& path = kernel->getPath (k);
                    if (path.count > 0 && path.first == 0 && path.input < numInputs && path.output < numOutputs)
                        accumulatePart (*kernel, which == 1, k, 0, head);
                }

        // 4) De vuelta al tiempo: valen las últimas P muestras de cada bloque de 2P.
        for (int o = 0; o < numOutputs; ++o)
        {
            auto& output = outputs[static_cast<size_t> (o)];
            const bool fading = pending != nullptr;
            if (output.used)
                fft.inverse (output.re.data(), output.im.data(), block.data());
            else
                std::fill (block.begin(), block.end(), 0.0f);
            if (! fading)
            {
                std::copy_n (block.begin() + partitionSize, partitionSize, output.fifo.begin());
                continue;
            }
            if (output.otherUsed)
                fft.inverse (output.otherRe.data(), output.otherIm.data(), otherBlock.data());
            else
                std::fill (otherBlock.begin(), otherBlock.end(), 0.0f);
            // Fundido lineal a lo largo del bloque: la última muestra ya es el kernel nuevo entero.
            const float step = 1.0f / static_cast<float> (partitionSize);
            for (int j = 0; j < partitionSize; ++j)
            {
                const float a = block[static_cast<size_t> (partitionSize + j)], b = otherBlock[static_cast<size_t> (partitionSize + j)];
                output.fifo[static_cast<size_t> (j)] = a + (b - a) * (static_cast<float> (j + 1) * step);
            }
        }
        audible = pending != nullptr ? pending : current;
        audibleFade = pending != nullptr;
        if (pending != nullptr)
        {
            current = pending;
            pending = nullptr;
        }
        restartAhead();
    }

    // Un trozo p del camino k sobre la entrada de hace (first + p) bloques, contado desde 'blockHead' (la FDL del bloque
    // que se está calculando), sumado al espectro de su salida.
    void accumulatePart (const ConvolutionKernel& kernel, bool other, int k, int p, int blockHead) noexcept
    {
        const auto& path = kernel.getPath (k);
        auto& output = outputs[static_cast<size_t> (path.output)];
        float* accRe = other ? output.otherRe.data() : output.re.data();
        float* accIm = other ? output.otherIm.data() : output.im.data();
        bool& used = other ? output.otherUsed : output.used;
        if (! used)
        {
            std::fill_n (accRe, bins, 0.0f);
            std::fill_n (accIm, bins, 0.0f);
            used = true;
        }
        const auto& input = inputs[static_cast<size_t> (path.input)];
        const int age = path.first + p; // este trozo multiplica a la entrada de hace 'age' bloques
        const int slot = (blockHead - age + maxPartitions) % maxPartitions;
        const float* xr = input.re.data() + slot * bins;
        const float* xi = input.im.data() + slot * bins;
        const float* hr = kernel.partitionRe (k, p);
        const float* hi = kernel.partitionIm (k, p);
        for (int b = 0; b < bins; ++b)
        {
            accRe[b] += xr[b] * hr[b] - xi[b] * hi[b];
            accIm[b] += xr[b] * hi[b] + xi[b] * hr[b];
        }
    }

    int partitionSize = 0, bins = 0, maxPartitions = 1, numInputs = 0, numOutputs = 0;
    int position = 0, head = 0;
    std::array<Input, maxInputs> inputs;
    std::array<Output, maxOutputs> outputs;
    std::vector<float> block, otherBlock;
    RealFft<float> fft;
    const ConvolutionKernel* current = nullptr;
    const ConvolutionKernel* pending = nullptr;
    const ConvolutionKernel* audible = nullptr;
    bool audibleFade = false;
    std::array<Ahead, 2> ahead {}; // lo adelantado del kernel que suena y del pendiente
};

} // namespace deepwater::dsp
