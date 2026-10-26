# SPDX-License-Identifier: BSD-3-Clause
"""Device admission without importing Torch or allocating accelerator memory."""
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

from tools.uroute.train import configure_device


class TrainingDeviceTest(unittest.TestCase):
    def torch(self, cuda=False, mps=True):
        return SimpleNamespace(
            cuda=SimpleNamespace(is_available=lambda: cuda, get_device_name=lambda: 'CUDA fixture'),
            backends=SimpleNamespace(mps=SimpleNamespace(is_available=lambda: mps)),
            mps=SimpleNamespace(recommended_max_memory=lambda: 8 * 1024 ** 3,
                                set_per_process_memory_fraction=Mock()))

    def test_auto_preserves_policy_without_touching_metal_allocator(self):
        for cuda, expected in [(False, 'cpu'), (True, 'cuda')]:
            torch = self.torch(cuda=cuda)
            self.assertEqual(configure_device(torch, 'auto')[0], expected)
            torch.mps.set_per_process_memory_fraction.assert_not_called()

    def test_explicit_metal_requires_a_bounded_allocator(self):
        torch = self.torch()
        for limit in [None, 0, -1, float('nan'), float('inf'), 1e308, 9, 1e-20]:
            with self.subTest(limit=limit), self.assertRaises(ValueError):
                configure_device(torch, 'mps', limit)
        torch.mps.set_per_process_memory_fraction.assert_not_called()
        device, runtime = configure_device(torch, 'mps', 4)
        self.assertEqual(device, 'mps')
        self.assertEqual(runtime['mps_allocator_limit_bytes'], 4 * 1024 ** 3)
        torch.mps.set_per_process_memory_fraction.assert_called_once_with(.5)

    def test_unavailable_devices_and_misplaced_limit_fail(self):
        torch = self.torch(mps=False)
        for device, limit in [('cuda', None), ('mps', 4), ('cpu', 4), ('auto', 4), ('invalid', None)]:
            with self.subTest(device=device), self.assertRaises(ValueError):
                configure_device(torch, device, limit)
        torch.mps.set_per_process_memory_fraction.assert_not_called()


if __name__ == '__main__':
    unittest.main()
