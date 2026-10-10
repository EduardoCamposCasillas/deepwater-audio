# Null test: resta dos carpetas de WAV (mismos nombres) y dice cuánto difieren.
#
#   python scripts/null_test.py <carpeta_referencia> <carpeta_nueva>
#
# Si el código no cambió el sonido, la resta es silencio digital (todas las muestras idénticas). Si no lo es, imprime
# la diferencia máxima en dB por debajo del pico de la referencia: por debajo de ~-120 dB es redondeo de coma flotante
# (inaudible); por encima, algo cambió de verdad. Requiere numpy.
import struct
import sys
from pathlib import Path

import numpy as np


def read_wav(path):
    data = path.read_bytes()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError(f'{path}: no es un WAV')
    pos, fmt, samples = 12, None, None
    while pos + 8 <= len(data):
        chunk, size = data[pos:pos + 4], struct.unpack('<I', data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if chunk == b'fmt ':
            fmt = struct.unpack('<HHIIHH', body[:16])
        elif chunk == b'data':
            samples = body
        pos += 8 + size + (size & 1)
    tag, channels, rate, _, _, bits = fmt
    if tag == 0xFFFE:  # WAVE_FORMAT_EXTENSIBLE: el formato real está en el subformato
        tag = struct.unpack('<H', data[data.find(b'fmt ') + 8 + 24:][:2])[0]
    if tag == 3 and bits == 32:
        audio = np.frombuffer(samples, dtype='<f4')
    elif tag == 1 and bits == 16:
        audio = np.frombuffer(samples, dtype='<i2') / 32768.0
    elif tag == 1 and bits == 24:
        raw = np.frombuffer(samples, dtype=np.uint8).reshape(-1, 3)
        audio = (raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16))
        audio = np.where(audio >= 1 << 23, audio - (1 << 24), audio) / float(1 << 23)
    else:
        raise ValueError(f'{path}: formato no soportado (tag {tag}, {bits} bits)')
    return audio.reshape(-1, channels).astype(np.float64), rate


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    if len(sys.argv) != 3:
        print(__doc__ or 'uso: null_test.py <referencia> <nueva>')
        return 2
    reference, candidate = Path(sys.argv[1]), Path(sys.argv[2])
    files = sorted(reference.glob('*.wav'))
    if not files:
        print(f'No hay WAV en {reference}')
        return 2
    worst_db, identical = -np.inf, True
    for ref_file in files:
        new_file = candidate / ref_file.name
        if not new_file.exists():
            print(f'{ref_file.name:24s} FALTA en {candidate}')
            identical = False
            continue
        a, rate_a = read_wav(ref_file)
        b, rate_b = read_wav(new_file)
        if a.shape != b.shape or rate_a != rate_b:
            print(f'{ref_file.name:24s} DISTINTO TAMAÑO: {a.shape} @ {rate_a} vs {b.shape} @ {rate_b}')
            identical = False
            continue
        diff = np.abs(a - b)
        peak = max(np.max(np.abs(a)), 1e-30)
        differing = int(np.count_nonzero(diff))
        if differing == 0:
            print(f'{ref_file.name:24s} idéntico bit a bit ({a.shape[0]} muestras x {a.shape[1]} canales)')
            continue
        identical = False
        db = 20 * np.log10(np.max(diff) / peak)
        worst_db = max(worst_db, db)
        print(f'{ref_file.name:24s} difiere en {differing} muestras; máx. {db:.1f} dB bajo el pico')
    if identical:
        print('NULL TEST: silencio digital (todo idéntico bit a bit)')
        return 0
    print(f'NULL TEST: hay diferencias (peor {worst_db:.1f} dB bajo el pico)')
    return 1


if __name__ == '__main__':
    sys.exit(main())
