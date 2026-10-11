# Comparación con el volumen igualado (gain-matched): mide la sonoridad de varias versiones de un mismo audio (sin
# procesar, con Riptide, con plugins de referencia) y escribe copias a la MISMA sonoridad para compararlas a oído.
#
#   python scripts/level_match.py <carpeta> [--ref <archivo.wav>] [--ciego]
#
# Por qué: el oído cree que lo más fuerte suena "mejor" (más lleno, más brillante, más cerca). Un saturador casi siempre
# sube el volumen, así que comparar sin igualar es comparar volúmenes, no timbres. Aquí todo se lleva a la sonoridad de
# la referencia (el audio sin procesar: un archivo que se llame dry*, original* o seco*, o el que diga --ref).
#
# Sonoridad = loudness integrada de la norma ITU-R BS.1770 (LUFS, la de los medidores de streaming): filtro K (realce de
# agudos como el de la cabeza + corte de graves por debajo de ~40 Hz, que el oído casi no oye como volumen), energía en
# ventanas de 400 ms y dos compuertas (descarta el silencio y las partes 10 LU por debajo de la media). Es mejor que el
# RMS para igualar porque pesa las frecuencias parecido a como las oímos.
#
# Escribe:
#   <carpeta>/igualados/<nombre>.wav   cada archivo con la ganancia que lo lleva a la sonoridad de la referencia
#   <carpeta>/ciego/A.wav, B.wav...   (con --ciego) las mismas copias (también la referencia) con nombres al azar, y
#                                      clave.txt para ver después cuál era cuál: escuchar sin saber qué es cada una
# e imprime una tabla: sonoridad, pico, cuánto sube cada versión, y el balance por octavas respecto de la referencia (ya
# igualada): dónde agrega o quita energía cada saturador. Requiere numpy. Lee WAV de 16/24 bits o float de 32.
import random
import struct
import sys
from pathlib import Path

import numpy as np

from null_test import read_wav

OCTAVES = [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]


def biquad_impulse(stages, length):
    # Respuesta al impulso de una cascada de biquads (b0, b1, b2, a1, a2), en forma directa II transpuesta.
    x = np.zeros(length)
    x[0] = 1.0
    for b0, b1, b2, a1, a2 in stages:
        y = np.empty(length)
        s1 = s2 = 0.0
        for n in range(length):
            out = b0 * x[n] + s1
            s1 = b1 * x[n] - a1 * out + s2
            s2 = b2 * x[n] - a2 * out
            y[n] = out
        x = y
    return x


def k_weighting(rate):
    # Los dos filtros del filtro K de BS.1770, recalculados para cualquier frecuencia de muestreo a partir de su
    # prototipo analógico (la norma solo da los coeficientes a 48 kHz; a 48 kHz estos coinciden con los de la norma).
    gain_db, fc, q = 3.999843853973347, 1681.974450955533, 0.7071752369554196
    k = np.tan(np.pi * fc / rate)
    vh = 10 ** (gain_db / 20)
    vb = vh ** 0.4996667741545416
    a0 = 1 + k / q + k * k
    shelf = ((vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
             2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0)
    fc, q = 38.13547087602444, 0.5003270373238773
    k = np.tan(np.pi * fc / rate)
    a0 = 1 + k / q + k * k
    high_pass = (1.0, -2.0, 1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0)
    return [shelf, high_pass]


def filter_fft(audio, impulse):
    # Convolución por FFT de cada canal con la respuesta al impulso (sin scipy).
    n = audio.shape[0] + impulse.size - 1
    size = 1 << (n - 1).bit_length()
    spectrum = np.fft.rfft(impulse, size)
    out = np.fft.irfft(np.fft.rfft(audio, size, axis=0) * spectrum[:, None], size, axis=0)
    return out[:audio.shape[0]]


def integrated_loudness(audio, rate):
    weighted = filter_fft(audio, biquad_impulse(k_weighting(rate), int(rate)))
    block, hop = int(0.4 * rate), int(0.1 * rate)  # ventanas de 400 ms que se solapan un 75 %
    if audio.shape[0] < block:
        return -np.inf
    starts = range(0, audio.shape[0] - block + 1, hop)
    power = np.array([np.sum(np.mean(weighted[s:s + block] ** 2, axis=0)) for s in starts])
    loudness = -0.691 + 10 * np.log10(np.maximum(power, 1e-30))
    gated = power[loudness > -70.0]  # compuerta absoluta: silencio
    if gated.size == 0:
        return -np.inf
    relative = -0.691 + 10 * np.log10(np.mean(gated)) - 10.0  # compuerta relativa: 10 LU bajo la media
    gated = power[(loudness > -70.0) & (loudness > relative)]
    return -0.691 + 10 * np.log10(np.mean(gated))


def octave_levels(audio, rate):
    # Energía por octava (media de los canales), en dB.
    spectrum = np.mean(np.abs(np.fft.rfft(audio, axis=0)) ** 2, axis=1)
    freqs = np.fft.rfftfreq(audio.shape[0], 1.0 / rate)
    levels = []
    for centre in OCTAVES:
        band = (freqs >= centre / np.sqrt(2)) & (freqs < centre * np.sqrt(2))
        levels.append(10 * np.log10(max(np.sum(spectrum[band]), 1e-30)))
    return np.array(levels)


def write_wav(path, audio, rate):
    data = audio.astype('<f4').tobytes()
    channels = audio.shape[1]
    header = b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE'
    header += b'fmt ' + struct.pack('<IHHIIHH', 16, 3, channels, rate, rate * channels * 4, channels * 4, 32)
    path.write_bytes(header + b'data' + struct.pack('<I', len(data)) + data)


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    args = sys.argv[1:]
    if not args or args[0].startswith('--'):
        print('uso: level_match.py <carpeta> [--ref <archivo.wav>] [--ciego]')
        return 2
    folder = Path(args[0])
    blind = '--ciego' in args
    files = sorted(folder.glob('*.wav'))
    if len(files) < 2:
        print(f'Hacen falta al menos 2 WAV en {folder} (la referencia y una versión procesada)')
        return 2
    if '--ref' in args:
        reference = folder / args[args.index('--ref') + 1]
    else:
        named = [f for f in files if f.stem.lower().startswith(('dry', 'original', 'seco'))]
        reference = named[0] if named else files[0]
    if reference not in files:
        print(f'No encuentro la referencia {reference}')
        return 2

    measured = {}
    for f in files:
        audio, rate = read_wav(f)
        measured[f] = (audio, rate, integrated_loudness(audio, rate))
    ref_audio, ref_rate, ref_lufs = measured[reference]
    if not np.isfinite(ref_lufs):
        print(f'La referencia {reference.name} es silencio')
        return 2

    print(f'Referencia: {reference.name} ({ref_lufs:.1f} LUFS)\n')
    print(f'{"archivo":28s} {"LUFS":>7s} {"pico":>7s} {"PLR":>6s} {"sube":>7s} {"ganancia":>9s}')
    matched_dir = folder / 'igualados'
    matched_dir.mkdir(exist_ok=True)
    gains, balances = {}, {}
    ref_octaves = octave_levels(ref_audio, ref_rate)
    for f, (audio, rate, lufs) in measured.items():
        peak = 20 * np.log10(max(np.max(np.abs(audio)), 1e-30))
        if not np.isfinite(lufs):
            print(f'{f.name:28s} silencio')
            continue
        gain_db = ref_lufs - lufs
        gains[f] = gain_db
        matched = audio * 10 ** (gain_db / 20)
        write_wav(matched_dir / f.name, matched, rate)
        # PLR (peak to loudness ratio): cuánto sobresalen los picos de la sonoridad. La saturación lo baja (redondea
        # picos): menos PLR = más denso, menos pegada.
        print(f'{f.name:28s} {lufs:7.1f} {peak:7.1f} {peak - lufs:6.1f} {lufs - ref_lufs:+7.1f} {gain_db:+9.1f} dB')
        if f != reference and rate == ref_rate:
            balances[f] = octave_levels(matched, rate) - ref_octaves

    if balances:
        print('\nBalance por octavas respecto de la referencia, con el volumen igualado (dB; + = más energía):')
        print(f'{"archivo":28s}' + ''.join(f'{(str(int(c)) if c >= 100 else str(c)):>7s}' for c in OCTAVES))
        for f, diff in balances.items():
            print(f'{f.name:28s}' + ''.join(f'{d:+7.1f}' for d in diff))
        print('(arriba de 4 kHz: brillo y "fizz"; 125–500 Hz: cuerpo o barro; 31–63 Hz: subgraves)')

    if blind:
        blind_dir = folder / 'ciego'
        blind_dir.mkdir(exist_ok=True)
        others = list(gains)  # también la referencia: ¿se distingue del procesado con el volumen igualado?
        random.shuffle(others)
        lines = []
        for letter, f in zip('ABCDEFGHIJKLMNOPQRSTUVWXYZ', others):
            (blind_dir / f'{letter}.wav').write_bytes((matched_dir / f.name).read_bytes())
            lines.append(f'{letter} = {f.name}')
        (blind_dir / 'clave.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        print(f'\nPrueba ciega: {len(others)} archivos en {blind_dir} (A, B, ...). Escúchalos y anota cuál prefieres y '
              f'por qué ANTES de abrir clave.txt.')
    print(f'\nCopias igualadas en {matched_dir}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
