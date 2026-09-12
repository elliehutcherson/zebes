"""Run 02: raised-heel forefoot support and a brisk, upright half-second gait."""

import math

try:
    from .run_motion import curve
except ImportError:
    from run_motion import curve

CYCLE_SECONDS = .5
SAMPLES = 24
STANCE_END = .28
STRIDE = 2.4
SPEED = STRIDE / CYCLE_SECONDS
ACTION = 'Storybook_Run_02'
# The measured toe-pad vertices remain the lowest points over this interval.
SUPPORT_PITCH = (.64, .92)


def paw(phase):
    """Toe-pad Y, lowest paw clearance and pitch; pad stays planted in stance."""
    phase %= 1
    start_y = -1.23
    end_y = start_y + STRIDE*STANCE_END
    pitch = curve(phase, [(0, .78, 0), (.10, .64, 0), (STANCE_END, .92, 0),
                          (.42, 1.12, 0), (.64, .20, -5), (.82, -.20, 0), (1, .78, 0)])
    if phase <= STANCE_END:
        return {'y': start_y+STRIDE*phase, 'clearance': 0., 'pitch': pitch, 'stance': True}
    y = curve(phase, [(STANCE_END, end_y, STRIDE), (.42, -.31, 0),
                      (.65, -.78, -4.8), (.85, -1.33, 0), (1, start_y, STRIDE)])
    clearance = curve(phase, [(STANCE_END, 0, 0), (.45, .43, 2),
                              (.61, .61, 0), (.82, .34, -3), (1, 0, 0)])
    return {'y': y, 'clearance': clearance, 'pitch': pitch, 'stance': False}


def body(phase):
    return {'bob': .035-.075*math.cos(4*math.pi*(phase-.10)),
            'lean': .09+.015*math.sin(4*math.pi*(phase-.10)),
            'arm_swing': -.72*math.cos(2*math.pi*phase)}
