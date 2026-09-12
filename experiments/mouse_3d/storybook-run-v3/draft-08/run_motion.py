"""Run timing and authored paw paths, independent of Blender and the engine."""

import math

CYCLE_SECONDS = 2 / 3
SAMPLES = 24
STANCE_END = .34
STRIDE = 2.1
SPEED = STRIDE / CYCLE_SECONDS


def hermite(a, b, da, db, t, duration):
    return ((2*t**3 - 3*t*t + 1)*a + (t**3 - 2*t*t + t)*duration*da
            + (-2*t**3 + 3*t*t)*b + (t**3 - t*t)*duration*db)


def curve(phase, keys):
    for (start, a, da), (end, b, db) in zip(keys, keys[1:]):
        if phase <= end:
            return hermite(a, b, da, db, (phase-start)/(end-start), end-start)
    raise ValueError('Curve does not cover the requested phase')


def paw(phase):
    """Ankle Y, lowest sole clearance, pitch; stance travels at virtual speed."""
    phase %= 1
    start_y = -.99
    end_y = start_y + STRIDE*STANCE_END
    if phase <= STANCE_END:
        return {'y': start_y + STRIDE*phase, 'clearance': 0., 'pitch': 0., 'stance': True}
    y = curve(phase, [(STANCE_END, end_y, STRIDE), (.50, .12, 0),
                      (.70, -.45, -5.0), (.88, -1.10, 0), (1., start_y, STRIDE)])
    clearance = curve(phase, [(STANCE_END, 0, 0), (.53, .44, 1.5),
                              (.68, .58, 0), (.86, .24, -2.5), (1., 0, 0)])
    pitch = curve(phase, [(STANCE_END, 0, 0), (.51, .95, 0),
                          (.70, .12, -3), (.86, -.12, 0), (1., 0, 0)])
    return {'y': y, 'clearance': clearance, 'pitch': pitch, 'stance': False}


def body(phase):
    return {'bob': -.17 - .085*math.cos(4*math.pi*(phase-.12)),
            'lean': .14 + .025*math.sin(4*math.pi*(phase-.12)),
            'arm_swing': -.60*math.cos(2*math.pi*phase)}
