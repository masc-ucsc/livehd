
# Contributions or Policy

This document explains the policy to handle contributions to the LGraph project. Most of this
document is inspired in LLVM developers policy.

## Submit of Contribution

LGraph has code owners for sections of the repository. When a new contribution is submitted, it must
be accepted by the current code owner. Check [CODE_OWNERS.txt](CODE_OWNERS.txt) to know who must
approve your pull request.

Only code owners can do direct commits to the repository. Code owners are also in charge of
accepting pull requests. External contributors can create pull requests to handle bugs, new
features, and any other code change. We recommend to contact code owners to discuss before doing any
large code change contribution.

Non code owners should create a pull request to submit patches. The details are explained in the
[Github-use.md](Github-use.md). The overall idea is to fork the LGraph repository, do your edits in
your local repository, and push the changes creating a pull request with "pull_request_XXX" were XXX
is your github account.

## Attribution of Contribution

When contributors submit a patch to LGraph, other developers with commit access may commit it for
the author once appropriate (based on the progression of code review, etc.). When doing so, it is
important to retain correct attribution of contributions to their contributors. However, we do not
want the source code to be littered with random attributions “this code written by J. Random Hacker”
(this is noisy and distracting). In practice, the revision control system keeps a perfect history of
who changed what, and the [CREDITS.txt](CREDITS.txt) file describes higher-level contributions.

If you commit code for other user, update the [CREDITS.txt](CREDITS.txt) as required.

## License

We intend to keep LGraph perpetually open source and to use a liberal open source
[license](../LICENSE) (BSD 3-Clause). As a contributor to the project, you agree that any
contributions be licensed under the terms of the BSD 3-Clause license shown at the root directory.
The BSD 3-Clause license boils down to this:

* You can freely distribute LGraph.
* You must retain the copyright notice if you redistribute LGraph.
* Binaries derived from LGraph must reproduce the copyright notice (e.g. in an included readme file).
* You can’t use our names to promote your LGraph derived products.
* There’s no warranty on LGraph at all.

We believe this fosters the widest adoption of LGraph because it allows commercial products to be
derived from LGraph with few restrictions and without a requirement for making any derived works
also open source (i.e. BSD 3-Clause license is not a “copyleft” license like the GPL). We suggest
that you read the License if further clarification is needed.

When you create a new code file, it should include "This file is distributed under the BSD 3-Clause
License. See LICENSE for details." in the first line of the file.

## Patents

The goal is to keep LGraph patent free. At the current state, only the code to perform incremental
synthesis has patents. This code is not enabled by default or used by LGraph unless explicitly
called. We are in the process of either remove it from the main LGraph repository or to grant access
to LGraph users.

When contributing code, we expect contributors to notify us of any potential for patent-related
trouble with their changes (including from third parties). If you or your employer own the rights to
a patent and would like to contribute code to LGraph that relies on it, we require that the copyright
owner sign an agreement that allows any other user of LGraph to freely use your patent.
