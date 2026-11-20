#!/usr/bin/env python3
"""Synthesizes the sounds of the vehicle templates. An engine is built the way it makes
its noise: one pressure pulse per cylinder firing, a little different every time, sent
through the pipes it rings in, with the hiss of air and the rattle of metal on top. Each
engine is made at several speeds, because the pipes ring at the same notes whatever the
speed: one recording played faster or slower is what makes an engine sound like a toy.
Everything is periodic, so it loops without a click. Needs numpy.

    python3 Tools/make_sounds.py some/folder
"""
import os, sys, wave
import numpy as np

RATE = 32000

def save(folder, name, samples, peak=0.85):
    os.makedirs(folder, exist_ok=True)
    samples = samples * (peak / max(np.max(np.abs(samples)), 1e-9))
    with wave.open(os.path.join(folder, name), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes((np.clip(samples, -1, 1) * 32767).astype("<i2").tobytes())

def shaped(signal, response):
    """Filters a periodic signal: `response(frequencies)` is what each frequency is multiplied by."""
    return np.fft.irfft(np.fft.rfft(signal) * response(np.fft.rfftfreq(len(signal), 1.0 / RATE)), len(signal))

def resonances(peaks):
    """Pipes and boxes: each (hertz, sharpness, gain) is a note the system rings at."""
    def response(f):
        f = np.maximum(f, 1.0)
        out = np.ones_like(f, dtype=complex) * 0.25
        for center, q, gain in peaks: out += gain / (1 + 1j * q * (f / center - center / f))
        return out * (f > 28) / (1 + (f / 5200.0) ** 2)
    return response

def noise(n, low, high, seed):
    rng = np.random.default_rng(seed)
    return shaped(rng.standard_normal(n), lambda f: np.exp(-((np.log(np.maximum(f, 1)) - np.log(np.sqrt(low * high))) / np.log(high / low) * 2.2) ** 2))

def engine(rpm, firing, pipes, pulse_ms=(3.5, 0.5), roughness=0.1, hiss=0.25, clatter=0.1, seconds=1.6, seed=1):
    """`firing`: when each cylinder fires, as fractions of the two turns one cycle takes,
    with which exhaust pipe it breathes into: [(0.0, 0), (0.25, 1), ...]."""
    rng = np.random.default_rng(seed)
    cycle = 120.0 / rpm
    cycles = max(2, round(seconds / cycle))
    n = int(round(cycles * cycle * RATE))
    banks = max(b for _, b in firing) + 1
    trains = np.zeros((banks, n))
    for c in range(cycles):
        for offset, bank in firing:
            when = ((c + offset) * cycle + rng.normal(0, 0.004) * cycle) * RATE
            strength = 1.0 + rng.normal(0, roughness)             # no two explosions are the same
            i = int(np.floor(when))
            trains[bank][i % n] += strength * (1 - (when - i))
            trains[bank][(i + 1) % n] += strength * (when - i)
    t = np.arange(n) / RATE
    slow, fast = pulse_ms[0] / 1000.0, pulse_ms[1] / 1000.0
    kernel = np.exp(-t / slow) - np.exp(-t / fast)                # a sharp rise and a slower fall
    kernel_f = np.fft.rfft(kernel)
    out = np.zeros(n)
    everything = np.zeros(n)
    for bank in range(banks):
        pulses = np.fft.irfft(np.fft.rfft(trains[bank]) * kernel_f, n)
        everything += pulses
        # Each bank has its own pipe, a little different from the other.
        out += shaped(pulses, resonances([(f * (1 + 0.07 * bank), q, g) for f, q, g in pipes]))
    out /= np.max(np.abs(out)) + 1e-9
    beat = everything / (np.max(np.abs(everything)) + 1e-9)
    breath = noise(n, 400, 3500, seed + 1)
    breath /= np.max(np.abs(breath)) + 1e-9
    metal = noise(n, 1800, 7000, seed + 2)
    metal /= np.max(np.abs(metal)) + 1e-9
    out = out + hiss * breath * (0.35 + 0.65 * np.abs(beat)) + clatter * metal * (0.3 + 0.7 * np.abs(beat)) ** 2
    return np.tanh(out * 1.6)                                     # pushed a little, as a hot exhaust is

def write_all(folder):
    # A four: one bang every half turn, evenly. Raspy, a little thin.
    four = [(0.0, 0), (0.25, 0), (0.5, 0), (0.75, 0)]
    for rpm in (1000, 2600, 4400, 6200):
        save(folder, "engine_i4_%d.wav" % rpm, engine(rpm, four, [(95, 3, 1.2), (190, 4, 0.9), (410, 5, 0.8), (980, 4, 0.5), (2300, 3, 0.3)],
                                                       roughness=0.08, hiss=0.22 + rpm / 20000.0, clatter=0.08 + rpm / 40000.0, seed=rpm))
    # An eight with a cross-plane crank: each bank gets its bangs unevenly spaced, which is the burble.
    eight = [(0.0, 0), (0.125, 1), (0.25, 1), (0.375, 0), (0.5, 1), (0.625, 0), (0.75, 0), (0.875, 1)]
    for rpm in (800, 2000, 3700, 5600):
        save(folder, "engine_v8_%d.wav" % rpm, engine(rpm, eight, [(62, 3, 1.6), (118, 4, 1.3), (240, 4, 0.9), (520, 4, 0.5), (1500, 3, 0.25)],
                                                       pulse_ms=(5.0, 0.7), roughness=0.14, hiss=0.2 + rpm / 22000.0, clatter=0.05 + rpm / 60000.0, seed=rpm + 7))
    # A diesel: harder, shorter bangs, and a lot of clatter at idle.
    for rpm in (900, 1700, 2800, 4200):
        save(folder, "engine_diesel_%d.wav" % rpm, engine(rpm, four, [(80, 3, 1.3), (170, 3, 1.0), (360, 4, 0.7), (1300, 3, 0.6), (3200, 3, 0.4)],
                                                           pulse_ms=(2.2, 0.25), roughness=0.12, hiss=0.3, clatter=0.45 - rpm / 14000.0, seed=rpm + 13))
    # A five: an odd number of cylinders, so its beats never line up with themselves: a warble.
    five = [(k / 5.0, 0) for k in range(5)]
    for rpm in (900, 2500, 4300, 6200):
        save(folder, "engine_i5_%d.wav" % rpm, engine(rpm, five, [(88, 3, 1.3), (176, 5, 1.1), (300, 4, 0.8), (760, 4, 0.55), (1900, 3, 0.3)],
                                                       pulse_ms=(3.8, 0.5), roughness=0.1, hiss=0.2 + rpm / 22000.0, clatter=0.07 + rpm / 45000.0, seed=rpm + 21))
    # A straight six: evenly spaced and balanced. Smooth, with a ripping edge high up.
    six = [(k / 6.0, 0) for k in range(6)]
    for rpm in (800, 2400, 4400, 6500):
        save(folder, "engine_i6_%d.wav" % rpm, engine(rpm, six, [(105, 3, 1.1), (210, 4, 1.0), (430, 5, 0.9), (1100, 4, 0.6), (2600, 3, 0.35)],
                                                       pulse_ms=(3.0, 0.45), roughness=0.05, hiss=0.2 + rpm / 20000.0, clatter=0.05 + rpm / 50000.0, seed=rpm + 33))
    # A twelve: two sixes. So many bangs that they run together into a howl.
    twelve = [(k / 12.0, k % 2) for k in range(12)]
    for rpm in (900, 2800, 5200, 8000):
        save(folder, "engine_v12_%d.wav" % rpm, engine(rpm, twelve, [(120, 3, 1.0), (260, 4, 1.0), (560, 5, 1.0), (1400, 4, 0.7), (3200, 3, 0.4)],
                                                        pulse_ms=(2.4, 0.35), roughness=0.04, hiss=0.18 + rpm / 20000.0, clatter=0.04 + rpm / 60000.0, seed=rpm + 45))
    # An electric motor: no bangs, and far quieter than an engine. What is heard is the
    # motor's own whine, pure and rising with its speed, a fainter one from the gears, the
    # steady high hiss of the electronics that feed it, and the air it stirs.
    for rpm in (1500, 4500, 9000, 14000):
        n = RATE * 2
        t = np.arange(n) / RATE
        turn = rpm / 60.0
        def whine(order, depth=0.0015, rate=4.0, phase=0.0):
            """A tone at a multiple of the motor's turning, held to a whole number of waves so that it loops, and wavering a hair."""
            f = np.round(turn * order * 2) / 2.0
            return np.sin(2 * np.pi * f * t + depth * f / rate * np.sin(2 * np.pi * rate * t + phase)) if f < RATE / 2.4 else 0.0
        motor = 1.0 * whine(8) + 0.4 * whine(4, phase=1.0) + 0.22 * whine(24, phase=2.0) + 0.1 * whine(48, phase=0.5)
        gears = 0.3 * whine(23 / 9.0, 0.003, 2.5) + 0.16 * whine(46 / 9.0, 0.003, 2.5, 1.3)
        electronics = 0.05 * np.sin(2 * np.pi * 9000 * t) * (0.7 + 0.3 * np.sin(2 * np.pi * np.round(turn * 4) * t))
        air = noise(n, 250, 2200, rpm)
        air = air / (np.max(np.abs(air)) + 1e-9) * (0.1 + 0.25 * rpm / 14000.0)
        save(folder, "engine_electric_%d.wav" % rpm, motor + gears + electronics + air, peak=0.42)
    # Rubber sliding: a howl that is nearly a note and never settles on one, over a rough hiss.
    n = RATE * 2
    howl = noise(n, 700, 1300, 21) * 3 + noise(n, 1500, 2600, 22) * 1.2 + noise(n, 250, 600, 23) * 0.8
    flutter = 0.6 + 0.4 * np.abs(noise(n, 4, 18, 24)) / np.max(np.abs(noise(n, 4, 18, 24)))
    save(folder, "skid.wav", np.tanh(howl / np.max(np.abs(howl)) * 2.2) * flutter, peak=0.7)
    # Wind: low and wide, gusting slowly.
    wind = shaped(np.random.default_rng(31).standard_normal(RATE * 3), lambda f: 1.0 / (1.0 + (f / 280.0) ** 2) + 0.02)
    save(folder, "wind.wav", wind, peak=0.7)
    # The tires on the road at speed: a rumble.
    save(folder, "road.wav", noise(RATE * 2, 40, 420, 41), peak=0.6)
    # A flat six behind a two-blade propeller: the blades slap the air twice a turn.
    six = [(i / 6.0, i % 2) for i in range(6)]
    for rpm in (1100, 2400):
        base = engine(rpm, six, [(70, 3, 1.4), (150, 4, 1.0), (330, 4, 0.7), (900, 3, 0.4)], pulse_ms=(4.0, 0.6), roughness=0.1, hiss=0.2, clatter=0.08, seed=rpm + 3)
        t = np.arange(len(base)) / RATE
        blades = np.round(len(base) / RATE * rpm / 60.0 * 2) / (len(base) / RATE)
        slap = shaped(np.sign(np.sin(2 * np.pi * blades * t)) * np.abs(np.sin(2 * np.pi * blades * t)) ** 0.3, lambda f: 1.0 / (1 + (f / 900.0) ** 2))
        save(folder, "propeller_%d.wav" % rpm, base * 0.6 + slap / np.max(np.abs(slap)) * 0.8)
    # The stall warning: a reed horn, on and off.
    t = np.arange(int(RATE * 0.6)) / RATE
    horn = (np.sin(2 * np.pi * 620 * t) + 0.5 * np.sin(2 * np.pi * 1240 * t) + 0.3 * np.sin(2 * np.pi * 1860 * t)) * (t < 0.36)
    save(folder, "stall.wav", horn * np.minimum(1, np.minimum(t, np.abs(0.36 - t)) / 0.01), peak=0.6)

if __name__ == "__main__":
    write_all(sys.argv[1] if len(sys.argv) > 1 else ".")
    print("wrote the sounds")
