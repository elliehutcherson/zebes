"""Authored head cross sections, shared by the skin and its surface details."""

import math

# x, vertical center, half width, half height. The skull, jaw and muzzle
# belong to this one surface; color changes do not introduce extra geometry.
HEAD_SECTIONS = (
    (-0.61, 3.25, 0.025, 0.035),
    (-0.56, 3.27, 0.22, 0.25),
    (-0.43, 3.28, 0.38, 0.42),
    (-0.23, 3.29, 0.47, 0.52),
    (0.00, 3.29, 0.49, 0.55),
    (0.21, 3.27, 0.46, 0.52),
    (0.40, 3.23, 0.40, 0.44),
    (0.56, 3.18, 0.32, 0.34),
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
    return 0.024 * math.exp(-((x - 0.43) / 0.145)**2 - ((z - 3.47) / 0.18)**2)


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
    eye = 0.38 * math.exp(-((x - 0.43)/0.22)**2 - ((z - 3.47)/0.25)**2)
    cream = 1 - (1 - muzzle) * (1 - chin) * (1 - eye)
    brown, pale = (0.46, 0.205, 0.072), (0.78, 0.55, 0.30)
    return tuple(a + (b - a)*cream for a, b in zip(brown, pale)) + (1,)
