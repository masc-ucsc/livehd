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

The document also explains how to add new submodules

## lgraph submodules

All the submodules are located in subs directory. The goal is to clone there
code with no code changes. Examples are ABC or YOSYS. If a subproject requires
code change, we place it in misc/xxx.

To add a submodule, use the submodule command

   # Example of adding abc
   git submodule add git@github.com:berkeley-abc/abc.git subs/abc

## Public lgraph GitHub Repo

If you do not need the private repo, just get the public repo by executing:

    git clone https://github.com/masc-ucsc/lgraph

## Private lgraph MASC Lab Repo

If you are working on lgraph at UC Santa Cruz, contact [Jose Renau](http://users.soe.ucsc.edu/~renau/)
to get access to the private lgraph repo used by the MASC lab. The clone the private lgraph Repo:

    git clone git@github.com:masc-ucsc/lgraph-private.git

If you are not tasked with synchronizing your work with the public repo then
you can simply push/pull changes to/from the private repo and someone else
will push them to the public one.

## Synchronizing Public and Private Repos

This section describes how we synchronize the public and private lgraph repos.
Most users can ignore it and simply work on the appropriate public or private 
repo.

### Create Private lgraph repo for first time

If you work outside UCSC, you should clone the pubic lgraph repo. First, create
an empty repository (lgraph-private), then run this to close lgraph

    # First
    git clone --bare https://github.com/masc-ucsc/lgraph
    cd lgraph.git
    git push --mirror https://github.com/yourname/lgraph-private.git
    cd ..
    rm -rf lgraph.git

### Typical usage

The workflow in the [MASC lab][masc] is as follows:

Suggested options for git first time users

    # Rebase no merge by default
    git config --global pull.rebase true
    # Set your name and email
    git config --global user.email "email@example.com"
    git config --global user.name "Mona Lisa"


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

[pull]: https://help.github.com/articles/creating-a-pull-request
[masc]: http://masc.soe.ucsc.edu/
