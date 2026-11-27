#!/usr/bin/env python3
#
# MutatedHypnoise: builds the audio-bootloader update file (.wav) for Braids.
#
# Python 3 port of stm_audio_bootloader/qpsk/encoder.py (Python 2 only),
# producing the same signal: QPSK, 48 kHz, 6 kHz carrier, 12000 bit/s,
# 256-byte packets with CRC32, a short blank after every 1 kB flash page
# (time for the bootloader to program it).
#
# Usage:  python braids/make_update_wav.py build/braids/braids.bin [out.wav]

import struct
import sys
import zlib

import numpy

SAMPLE_RATE = 48000
CARRIER_FREQUENCY = 6000
BIT_RATE = 12000
PACKET_SIZE = 256
PAGE_SIZE = 1024
PAGE_BLANK_DURATION = 0.06

# The bootloader programs the firmware from 0x08004000; the settings pages
# start at 0x0801F000. Anything larger would overwrite them.
MAX_FIRMWARE_SIZE = 0x0801F000 - 0x08004000


class QpskEncoder(object):

  def __init__(self, sample_rate, carrier_frequency, bit_rate, packet_size):
    period = sample_rate // carrier_frequency
    symbol_time = sample_rate // (bit_rate // 2)
    assert (symbol_time % period == 0) or (period % symbol_time == 0)
    assert sample_rate % carrier_frequency == 0
    assert sample_rate % bit_rate == 0
    self._sr = sample_rate
    self._br = bit_rate
    self._carrier_frequency = carrier_frequency
    self._sample_index = 0
    self._packet_size = packet_size

  def _encode(self, symbols):
    ratio = self._sr // self._br * 2
    symbols = numpy.asarray(symbols, dtype=int)
    even = numpy.repeat(2 * (symbols % 2) - 1, ratio) / numpy.sqrt(2.0)
    odd = numpy.repeat(2 * (symbols // 2) - 1, ratio) / numpy.sqrt(2.0)
    t = (numpy.arange(len(even)) + self._sample_index) / float(self._sr)
    self._sample_index += len(even)
    phase = 2 * numpy.pi * self._carrier_frequency * t
    return even * numpy.sin(phase) + odd * numpy.cos(phase)

  def _blank(self, duration):
    return self._encode([0] * (int(duration * self._br / 8) * 4))

  def _packet(self, data):
    data = data + b'\x00' * (self._packet_size - len(data))
    crc = zlib.crc32(data) & 0xffffffff
    preamble = [0] * 8 + [0x99] * 4 + [0xcc] * 4
    crc_bytes = [crc >> 24, (crc >> 16) & 0xff, (crc >> 8) & 0xff, crc & 0xff]
    symbols = []
    for byte in preamble + list(data) + crc_bytes:
      symbols += [(byte >> 6) & 3, (byte >> 4) & 3, (byte >> 2) & 3, byte & 3]
    return self._encode(symbols)

  def code(self, data):
    yield numpy.zeros(self._sr)  # 1 s of silence
    yield self._blank(1.0)
    if len(data) % PAGE_SIZE:
      data += b'\x00' * (PAGE_SIZE - len(data) % PAGE_SIZE)
    packets_per_page = PAGE_SIZE // self._packet_size
    for index, offset in enumerate(range(0, len(data), self._packet_size)):
      yield self._packet(data[offset:offset + self._packet_size])
      if (index + 1) % packets_per_page == 0:
        yield self._blank(PAGE_BLANK_DURATION)
    yield self._blank(1.0)


def write_wav(path, blocks, sample_rate):
  # Same quantization as the original writer: x * 32767, truncated.
  pcm = b''.join(
      (numpy.clip(b, -1.0, 1.0) * 32767).astype('<i2').tobytes()
      for b in blocks)
  with open(path, 'wb') as f:
    f.write(b'RIFF' + struct.pack('<L', 36 + len(pcm)) + b'WAVEfmt ')
    f.write(struct.pack('<LHHLLHH', 16, 1, 1, sample_rate, sample_rate * 2,
                        2, 16))
    f.write(b'data' + struct.pack('<L', len(pcm)))
    f.write(pcm)


def main():
  if len(sys.argv) not in (2, 3):
    sys.exit(__doc__ or 'usage: make_update_wav.py firmware.bin [out.wav]')
  firmware = open(sys.argv[1], 'rb').read()
  if len(firmware) > MAX_FIRMWARE_SIZE:
    sys.exit('Firmware is %d bytes, more than the %d available.' % (
        len(firmware), MAX_FIRMWARE_SIZE))
  output = sys.argv[2] if len(sys.argv) == 3 else (
      sys.argv[1].replace('.bin', '') + '.wav')
  encoder = QpskEncoder(SAMPLE_RATE, CARRIER_FREQUENCY, BIT_RATE, PACKET_SIZE)
  write_wav(output, encoder.code(firmware), SAMPLE_RATE)
  print('%s: %d bytes of firmware -> %s' % (sys.argv[1], len(firmware), output))


if __name__ == '__main__':
  main()
