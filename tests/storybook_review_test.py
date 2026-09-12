"""Shared review timing must preserve the retained run playback."""

import math
from pathlib import Path
import unittest

from PIL import Image

from scripts.review_storybook_run import durations


ROOT = Path(__file__).resolve().parents[1] / 'experiments/mouse_3d'


class StorybookReviewTimingTest(unittest.TestCase):
    def test_timing_matches_decoded_run_artifacts(self):
        for revision, seconds in ((1, 2/3), (2, .5), (3, 16/30)):
            result = ROOT / f'storybook-run-v{revision}' / 'result'
            prefix = 'game' if revision == 3 else 'run'
            for suffix, quantum in (('128-24.apng', 1), ('128-12.apng', 1), ('128.gif', 10)):
                path = result / f'{prefix}-{suffix}'
                with self.subTest(path=path), Image.open(path) as animation:
                    observed = []
                    for index in range(animation.n_frames):
                        animation.seek(index)
                        observed.append(animation.info['duration'])
                    self.assertEqual(durations(len(observed), seconds, quantum), observed)

    def test_rounding_preserves_total_and_bounds_frame_jitter(self):
        for count in (12, 16, 24, 48):
            for quantum in (1, 10):
                with self.subTest(count=count, quantum=quantum):
                    times = durations(count, 16/30, quantum)
                    self.assertLessEqual(abs(sum(times)-1000*16/30), quantum/2)
                    self.assertLessEqual(max(times)-min(times), quantum)
                    self.assertTrue(all(t > 0 and t % quantum == 0 for t in times))

    def test_invalid_or_unrepresentable_timing_fails(self):
        for args in ((0, 1), (-1, 1), (24, 0), (24, -1),
                     (24, math.nan), (24, math.inf), (24, 1, 0),
                     (24, 1, -1), (24, .001), (24, .1, 10)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                durations(*args)


if __name__ == '__main__':
    unittest.main()
