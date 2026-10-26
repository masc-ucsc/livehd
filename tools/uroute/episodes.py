# SPDX-License-Identifier: BSD-3-Clause
"""Deterministic task sampling shared by the offline trainer and its checks."""
from collections import deque
import math


class ImportanceSampler:
    def __init__(self, tasks, mode='sqrt-mean-floor'):
        if mode not in ('sqrt-mean-floor', 'paper-rms'):
            raise ValueError('unknown task sampling mode')
        self.mode = mode
        self.tasks = tuple(sorted(tasks))
        if not self.tasks:
            raise ValueError("training requires at least one task")
        self.losses = {task: deque(maxlen=10) for task in self.tasks}

    def probabilities(self):
        if self.mode == 'paper-rms':
            weights = []
            for task in self.tasks:
                history = self.losses[task]
                scale = max(history) if history else 1.
                # Scale before squaring so finite historical losses cannot
                # overflow while estimating sqrt(E[L_t^2]) from Eq. 15.
                weights.append(scale * math.sqrt(math.fsum((v / scale) ** 2 for v in history) / len(history))
                               if history and scale else scale)
            scale = max(weights)
            relative = [value / scale for value in weights] if scale else [1.] * len(weights)
            total = math.fsum(relative)
            # Algorithm 1 mixes one percent uniform probability, rather than
            # adding .01 to unnormalized task weights.
            return [.99 * value / total + .01 / len(self.tasks) for value in relative]
        weights = [math.sqrt(sum(self.losses[t]) / len(self.losses[t])) + .01 if self.losses[t] else 1.01
                   for t in self.tasks]
        total = sum(weights)
        return [value / total for value in weights]

    def sample(self, rng):
        probabilities = self.probabilities()
        task = rng.choices(self.tasks, weights=probabilities, k=1)[0]
        return task, probabilities[self.tasks.index(task)]

    def observe(self, task, loss):
        if task not in self.losses or not math.isfinite(loss) or loss < 0:
            raise ValueError("task losses must be finite and nonnegative")
        self.losses[task].append(loss)

    def state(self):
        return [{"task": list(task), "probability": p, "losses": list(self.losses[task])}
                for task, p in zip(self.tasks, self.probabilities())]
