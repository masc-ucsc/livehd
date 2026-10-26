# SPDX-License-Identifier: BSD-3-Clause
"""URoute architecture v1. See README.md for explicit paper ambiguities.

PyTorch is used only by the user's offline training/export process. LiveHD's
runtime implements the same operators in serial native C++.
"""
import torch
from torch import nn
from torch.nn import functional as F

CHANNELS = 64
LEVELS = 4


class Residual(nn.Module):
    def __init__(self, inputs):
        super().__init__()
        self.conv1 = nn.Conv2d(inputs, CHANNELS, 3, padding=1)
        self.conv2 = nn.Conv2d(CHANNELS, CHANNELS, 3, padding=1)
        self.skip = nn.Identity() if inputs == CHANNELS else nn.Conv2d(inputs, CHANNELS, 1)

    def forward(self, x):
        return F.relu(self.conv2(F.relu(self.conv1(x))) + self.skip(x))


class Matching(nn.Module):
    def __init__(self):
        super().__init__()
        self.z = nn.Conv2d(2 * CHANNELS, CHANNELS, 3, padding=1)
        # Figure 4 uses a spatial attention map after average pooling. Version 1
        # fixes this to channel averaging, keeping the spatial grid intact.
        self.gate = nn.Conv2d(1, 1, 7, padding=3)
        self.out = nn.Conv2d(CHANNELS, CHANNELS, 1)

    def forward(self, query, support):
        total = torch.zeros_like(query)
        for sample in support.unbind(1):
            z = F.relu(self.z(torch.cat((query, sample), dim=1)))
            weight = torch.sigmoid(self.gate(z.mean(dim=1, keepdim=True)))
            total = total + weight * sample
        return F.relu(self.out(total))


class URoute(nn.Module):
    def __init__(self):
        super().__init__()
        self.q_embed = nn.Conv2d(4, CHANNELS, 3, padding=1)
        self.s_embed = nn.Conv2d(5, CHANNELS, 3, padding=1)
        self.query = nn.ModuleList(Residual(CHANNELS) for _ in range(LEVELS))
        self.support = nn.ModuleList(Residual(CHANNELS) for _ in range(LEVELS))
        self.matches = nn.ModuleList(Matching() for _ in range(LEVELS))
        self.decoder = nn.ModuleList(Residual(2 * CHANNELS) for _ in range(LEVELS))
        self.output = nn.Conv2d(CHANNELS, 1, 1)
        # Keep the final ReLU live when starting a new model from scratch.
        nn.init.constant_(self.output.bias, .1)

    def forward(self, query, support, *, linear_head=False):
        """query [B,4,X,Y], support [B,N,5,X,Y], output [B,1,X,Y]."""
        if query.ndim != 4 or query.shape[1] != 4 or min(query.shape[-2:]) < 8:
            raise ValueError("query must be B,4,X,Y with X,Y >= 8")
        if (support.ndim != 5 or support.shape[0] != query.shape[0]
                or support.shape[2] != 5 or support.shape[-2:] != query.shape[-2:]
                or not 1 <= support.shape[1] <= 16):
            raise ValueError("support must be B,N,5,X,Y with N in 1..16")
        batch, shots = support.shape[:2]
        q = F.relu(self.q_embed(query))
        s = F.relu(self.s_embed(support.flatten(0, 1)))
        queries, matched = [], []
        for level in range(LEVELS):
            q = self.query[level](q)
            s = self.support[level](s)
            queries.append(q)
            matched.append(self.matches[level](q, s.unflatten(0, (batch, shots))))
            if level + 1 != LEVELS:
                size = (q.shape[-2] // 2, q.shape[-1] // 2)
                q = F.interpolate(q, size=size, mode="bilinear", align_corners=False)
                s = F.interpolate(s, size=size, mode="bilinear", align_corners=False)
        out = queries[-1]
        for level in reversed(range(LEVELS)):
            if out.shape[-2:] != matched[level].shape[-2:]:
                out = F.interpolate(out, size=matched[level].shape[-2:], mode="bilinear", align_corners=False)
            out = self.decoder[level](torch.cat((matched[level], out), dim=1))
        out = self.output(out)
        # The optional training loss can see negative preactivations, so a
        # clamped-zero output still receives gradients. Exported inference
        # retains exactly the architecture-v1 nonnegative head.
        return out if linear_head else F.relu(out)
