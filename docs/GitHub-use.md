# lgraph and GitHub

lgraph is the synthesis/emulation flow primarily maintained and developed by
the [MASC lab][masc] at UC Santa Cruz.  Since lgraph is used for computer
architecture and VLSI research, the [MASC lab][masc] does development using a
private repo so that we can wait until the research is published before pushing
changes to the public repo hosted on GitHub.

This document describes the technique used at by the [MASC lab][masc] for
maintaining a private lgraph repo and integrating changes with the public repo
that is hosted on GitHub.  Other groups may choose to adapt this technique for
their own use.

lgraph uses bazel as a build system, as a result, we no longer use submodules.
Instead we use the built-in bazel support to pull specific repositories.

## lgraph GitHub Repo

You can clone the repository from:

    git clone https://github.com/masc-ucsc/lgraph

If you are working on lgraph at UC Santa Cruz, contact [Jose Renau](http://users.soe.ucsc.edu/~renau/)
to be added to the MASC organization on GitHub so that you have write access to
the repo.

External contributions are welcome through pull-requests (see bellow).

### Create Private lgraph repo for personal use / contributions

If you work outside UCSC, you should clone the pubic lgraph repo. First, create
an empty repository (lgraph-private), then run this to close lgraph

    # First
    git clone --bare https://github.com/masc-ucsc/lgraph
    cd lgraph.git
    git push --mirror https://github.com/yourname/lgraph-private.git
    cd ..
    rm -rf lgraph.git

### Simple contribution flow

The simplest way to contribute to LGraph is to create a fork, and a pull request. The overall flow is:

1. Fork it ( https://github.com/masc-ucsc/lgraph/fork )

2. Create your feature branch 

    git checkout -b my-nice-patch

3. Commit your changes 

    git commit -am 'Solve issue-x with bla bla'

4. Push to the branch 

    git push origin my-nice-patch

5. Create new [pull][pull] request

### Typical usage for frequent contributors

The workflow in the [MASC lab][masc] is as follows:

Suggested options for git first time users

    # Rebase no merge by default
    git config --global pull.rebase true
    # Set your name and email
    git config --global user.email "perico@example.com"
    git config --global user.name "Perico LosPalotes"
    git config --global pull.rebase true
    git config --global rebase.autoStash true

Work in the lgraph-private repo, and commit to master branch

    git clone https://github.com/yourname/lgraph-private.git lgraph
    cd lgraph
    make some changes
    git commit
    git push origin master

To pull latest version of code from lgraph public repository

    cd lgraph
    git remote add public https://github.com/masc-ucsc/lgraph
    git pull public master # Creates a merge commit
    git push origin master

To push your edits to the main public lgraph repo (replace XXX by your github name)

    git clone https://github.com/masc-ucsc/lgraph lgraph-public
    cd lgraph-public
    git remote add lgraph-private git@github.com:masc-ucsc/lgraph-masc.git  # Replace for your private repo
    git checkout -b pull_request_XXX
    git pull lgraph-private master
    git push origin pull_request_XXX

Now create a [pull][pull] request through github, and the UCSC/MASC team will review it.

### Rebase vs No-Rebase

Rebase creates cleaner logs, but sometimes it gets difficult to fix conflicts with rebase. For cases that you
are struggling to merge a conflict, you could do this:

    # undo the failed rebase merge
    git rebase --abort 

    # make sure that your code changes were committed
    git commit -a -m"Your commit message"
    git pull --no-rebase

    # Fix the conflict without rebase (easier)
    git commit -a -m"your merge message"
    git pull --no-rebase
    git push

### Typical git commands

Clean the directory from any file not in git (it will remove all the files not committed)

    git clean -fdx

Save and restore un-committed changes to allow a new git pull. stash is like a "push" and "pop" replays
the changes in the current directory. This will happen automatically if you have the autoStash configuration option.

    git stash
    git pull
    git stash pop

See the differences against the server (still not pushed). Everything may be committed, so git diff may be empty

    git diff @{u}

### Git Hercules statistics

    hercules --languages C++ --burndown --burndown-people --pb https://github.com/masc-ucsc/lgraph >hercules1.data
    labours -f pb -m overwrites-matrix -o hercules1a.pdf <hercules1.data
    labours -f pb -m ownership -o hercules1b.pdf <hercules1.data

    hercules --languages C++ --burndown --first-parent --pb https://github.com/masc-ucsc/lgraph >hercules2.data
    labours -f pb -m burndown-project -o hercules2.pdf <hercules2.data

    hercules --languages C++ --devs --pb https://github.com/masc-ucsc/lgraph >hercules3.data
    labours -f pb -m old-vs-new -o hercules3a.pdf <hercules3.data 
    labours -f pb -m devs -o hercules3b.pdf <hercules3.data
    labours -f pb -m devs-efforts -o hercules3c.pdf <hercules3.data

[pull]: https://help.github.com/articles/creating-a-pull-request
[masc]: http://masc.soe.ucsc.edu/
