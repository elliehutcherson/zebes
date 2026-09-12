"""Run 03 timing and paths for the foot study's articulated toe chains."""

import math

try:
    from .run_motion import curve
except ImportError:
    from run_motion import curve

CYCLE_SECONDS = 16 / 30
SAMPLES = 24
STANCE_END = .32
STRIDE = 2.6
SPEED = STRIDE / CYCLE_SECONDS
ACTION = 'Storybook_Run_03'


def paw(phase):
    """Support-point travel, clearance, raised-foot rotation and toe roll."""
    phase %= 1
    start = -1.42
    end = start + STRIDE * STANCE_END
    foot = curve(phase, [(0, .08, 0), (.11, -.12, 0), (.32, .70, 3),
                         (.46, 1.10, 0), (.64, .40, -5), (.84, -.16, 0), (1, .08, 0)])
    toe = curve(phase, [(0, 0, 0), (.18, 0, 0), (.32, .42, 3),
                        (.47, .90, 0), (.70, .08, -4), (.86, -.12, 0), (1, 0, 0)])
    stance = phase <= STANCE_END
    y = start + STRIDE*phase if stance else curve(phase, [
        (.32, end, STRIDE), (.46, -.39, 0), (.68, -1.10, -5),
        (.88, -1.55, 0), (1, start, STRIDE)])
    clearance = 0 if stance else curve(phase, [
        (.32, 0, 0), (.46, .28, 2.2), (.62, .44, 0),
        (.82, .25, -2.4), (1, 0, 0)])
    return {'y': y, 'clearance': clearance, 'foot_pitch': foot,
            'toe_pitch': toe, 'stance': stance}


def body(phase):
    return {'bob': -.04-.09*math.cos(4*math.pi*(phase-.10)),
            'lean': .07+.012*math.sin(4*math.pi*(phase-.10)),
            'twist': .065*math.cos(2*math.pi*phase)}


def arm(phase):
    # Back at same-side foot contact; forward as that foot leaves support.
    swing = -.88*math.cos(2*math.pi*(phase+.04))
    return {'swing': swing,
            'bend': 1.30+.32*math.sin(2*math.pi*(phase-.08)),
            'wrist': .14*math.sin(2*math.pi*(phase-.08))}
