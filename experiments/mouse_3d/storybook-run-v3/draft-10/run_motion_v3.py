"""Run 03 timing and paths for the foot study's articulated toe chains."""

import math

try:
    from .run_motion import curve
except ImportError:
    from run_motion import curve

CYCLE_SECONDS = 16 / 30
SAMPLES = 24
STANCE_END = .36
STRIDE = 3.05
SPEED = STRIDE / CYCLE_SECONDS
ACTION = 'Storybook_Run_03'


def paw(phase):
    """Support-point travel, clearance, raised-foot rotation and toe roll."""
    phase %= 1
    start = -1.62
    end = start + STRIDE * STANCE_END
    foot = curve(phase, [(0, .12, 0), (.11, -.06, 0), (.36, .82, 3),
                         (.48, 1.10, 0), (.66, .35, -5), (.84, -.10, 0), (1, .12, 0)])
    toe = curve(phase, [(0, 0, 0), (.20, 0, 0), (.36, .48, 3),
                        (.49, .85, 0), (.70, .08, -3), (.86, -.08, 0), (1, 0, 0)])
    stance = phase <= STANCE_END
    y = start + STRIDE*phase if stance else curve(phase, [
        (.36, end, STRIDE), (.48, -.32, 0), (.69, -1.24, -5.5),
        (.88, -1.78, 0), (1, start, STRIDE)])
    clearance = 0 if stance else curve(phase, [
        (.36, 0, 0), (.49, .20, 1.4), (.62, .27, 0),
        (.82, .09, -1.2), (1, 0, 0)])
    return {'y': y, 'clearance': clearance, 'foot_pitch': foot,
            'toe_pitch': toe, 'stance': stance}


def body(phase):
    return {'bob': -.14-.055*math.cos(4*math.pi*(phase-.10)),
            'lean': .19+.012*math.sin(4*math.pi*(phase-.10)),
            'twist': .065*math.cos(2*math.pi*phase)}


def arm(phase):
    # Back at same-side foot contact; forward as that foot leaves support.
    swing = -.88*math.cos(2*math.pi*(phase+.04))
    return {'swing': swing,
            'bend': 1.30+.32*math.sin(2*math.pi*(phase-.08)),
            'wrist': .14*math.sin(2*math.pi*(phase-.08))}
