"""Authored head cross sections, shared by the skin and its surface details."""

import math

EYE_X = 0.41
EYE_Z = 3.46

# x, vertical center, half width, half height. The skull, jaw and muzzle
# belong to this one surface; color changes do not introduce extra geometry.
HEAD_SECTIONS = (
    (-0.61, 3.25, 0.025, 0.035),
    (-0.56, 3.27, 0.22, 0.25),
    (-0.43, 3.28, 0.38, 0.42),
    (-0.23, 3.29, 0.485, 0.52),
    (0.00, 3.29, 0.515, 0.55),
    (0.21, 3.27, 0.49, 0.52),
    (0.40, 3.23, 0.425, 0.44),
    (0.56, 3.18, 0.34, 0.34),
    (0.72, 3.13, 0.235, 0.235),
    (0.89, 3.12, 0.16, 0.16),
    (1.035, 3.14, 0.105, 0.085),
    (1.08, 3.145, 0.065, 0.05),
)


def head_section(x):
    if not HEAD_SECTIONS[0][0] <= x <= HEAD_SECTIONS[-1][0]:
        raise ValueError(f"Head surface x is outside its authored domain: {x}")
    i = next((i for i in range(len(HEAD_SECTIONS) - 1) if x <= HEAD_SECTIONS[i + 1][0]), None)
    a, b = HEAD_SECTIONS[i:i + 2]
    t = (x - a[0]) / (b[0] - a[0])
    before, after = HEAD_SECTIONS[max(0, i - 1)], HEAD_SECTIONS[min(len(HEAD_SECTIONS) - 1, i + 2)]
    result = []
    for k in (1, 2, 3):
        left = (b[k] - before[k]) / (b[0] - before[0])
        right = (after[k] - a[k]) / (after[0] - a[0])
        result.append((2*t**3 - 3*t**2 + 1)*a[k] + (t**3 - 2*t**2 + t)*(b[0]-a[0])*left
                      + (-2*t**3 + 3*t**2)*b[k] + (t**3 - t**2)*(b[0]-a[0])*right)
    return result


def socket_depth(x, z):
    return 0.026 * math.exp(-((x - EYE_X) / 0.17)**2 - ((z - EYE_Z) / 0.19)**2)


def shorten_muzzle(x):
    """Smoothly shorten the snout while leaving the cranium unscaled."""
    distance = max(0, x - 0.40)
    return x - 0.26 * distance**2 / (distance + 0.10)


def head_side(x, z, sign):
    center, width, height = head_section(x)
    normalized = (z - center) / height
    if abs(normalized) >= 1:
        raise ValueError(f"Facial landmark is outside head surface: {(x, z)}")
    return sign * (width * math.sqrt(1 - normalized**2) - socket_depth(x, z))


def head_point(x, angle):
    center, width, height = head_section(x)
    z = center + height * math.sin(angle)
    y = width * math.cos(angle)
    y -= math.copysign(socket_depth(x, z) * abs(math.cos(angle))**8, y)
    return (x, y, z)


def fur_color(point):
    x, y, z = point
    muzzle = 1 / (1 + math.exp(-((x - 0.53)*14 + (3.32 - z)*5)))
    chin = 1 / (1 + math.exp(-((3.04 - z)*13 + (x - 0.1)*3)))
    eye = 0.38 * math.exp(-((x - EYE_X)/0.22)**2 - ((z - EYE_Z)/0.25)**2)
    cream = 1 - (1 - muzzle) * (1 - chin) * (1 - eye)
    brown, pale = (0.46, 0.205, 0.072), (0.78, 0.55, 0.30)
    return tuple(a + (b - a)*cream for a, b in zip(brown, pale)) + (1,)


def trouser_radius(t, angle, side):
    """Uneven fabric gathers localized to the knee and boot, not ring bands."""
    profile = ((0, 0.30), (0.13, 0.335), (0.30, 0.345), (0.49, 0.285),
               (0.61, 0.26), (0.72, 0.225), (0.85, 0.165), (1, 0.155))
    if not 0 <= t <= 1 or side not in ("near", "far"):
        raise ValueError("Trousers require a valid limb side and normalized distance")
    a, b = next((a, b) for a, b in zip(profile, profile[1:]) if a[0] <= t <= b[0])
    blend = (t-a[0]) / (b[0]-a[0])
    blend = blend*blend*(3-2*blend)
    radius = a[1] + (b[1]-a[1])*blend
    folds = ((0.29, -0.10, 0.025, 0.028, -1.25, 1.00),
             (0.43, 0.065, 0.036, 0.024, -1.50, 0.90),
             (0.515, -0.05, 0.044, 0.021, -2.50, 1.10),
             (0.595, 0.05, 0.032, 0.023, -2.15, 0.85),
             (0.70, -0.04, 0.030, 0.018, -1.10, 1.00),
             (0.775, 0.032, 0.029, 0.018, -1.90, 1.10))
    displacement = 0
    shift = 0.011 if side == "far" else 0
    for center, slope, amplitude, width, direction, spread in folds:
        across = math.atan2(math.sin(angle-direction), math.cos(angle-direction))
        along = (t-center-shift-slope*math.cos(angle+0.35)) / width
        ridge = math.exp(-along*along) - 0.55*math.exp(-(along+1.55)**2)
        displacement += amplitude*ridge*math.exp(-(across/spread)**2)
    return radius + displacement, displacement


def iris_color(u, v, radius):
    """Warm iris with a large, slightly forward-looking pupil."""
    pupil_distance = math.hypot(u - 0.11, v - 0.04)
    pupil = min(1, max(0, (pupil_distance - 0.51)/0.10))
    edge = min(1, max(0, (radius - 0.84)/0.12))
    lower_light = 0.80 + 0.20*max(0, -v)
    iris = (0.24*lower_light, 0.104*lower_light, 0.028*lower_light)
    dark = (0.010, 0.005, 0.003)
    return tuple((a + (b-a)*pupil)*(1-edge*0.76) for a, b in zip(dark, iris)) + (1,)
