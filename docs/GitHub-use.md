# LiveHD and GitHub

LiveHD is the synthesis/emulation flow primarily maintained and developed by
the [MASC lab][masc] at UC Santa Cruz.  Since LiveHD is used for computer
architecture and VLSI research, the [MASC lab][masc] has an internal private
repo for some still in progress works. This is done using a private repo so
that we can wait until the research is published before pushing changes to the
public repo hosted on GitHub. 

This guide explains how we use git at the MASC group, and how you could setup a
similar flow to contribute to the LiveHD project.  Other groups may choose to
adapt this technique for their own use.

LiveHD uses bazel as a build system, as a result, we no longer use submodules.
Instead we use the built-in bazel support to pull specific repositories.

## Github Configuration and Commands

This section is for git first time users and to show the git configuration used
by the MASC group.


### Configuration

Suggested options for git first time users

    # Rebase no merge by default
    git config --global pull.rebase true
    # Set your name and email
    git config --global user.email "perico@lospalotes.com"
    git config --global user.name "Perico LosPalotes"
    git config --global pull.rebase true
    git config --global rebase.autoStash true


### Rebase vs No-Rebase

Rebase creates cleaner logs, but sometimes it gets difficult to fix conflicts
with rebase. For cases that you are struggling to merge a conflict, you could
do this:

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

Save and restore un-committed changes to allow a new git pull. stash is like a
"push" and "pop" replays the changes in the current directory. This will happen
automatically if you have the autoStash configuration option.

    git stash
    git pull
    git stash pop

See the differences against the server (still not pushed). Everything may be
committed, so git diff may be empty

    git diff @{u}

### Git Hercules statistics

    hercules --languages C++ --burndown --burndown-people --pb https://github.com/masc-ucsc/livehd >hercules1.data
    labours -f pb -m overwrites-matrix -o hercules1a.pdf <hercules1.data
    labours -f pb -m ownership -o hercules1b.pdf <hercules1.data

    hercules --languages C++ --burndown --first-parent --pb https://github.com/masc-ucsc/livehd >hercules2.data
    labours -f pb -m burndown-project -o hercules2.pdf <hercules2.data

    hercules --languages C++ --devs --pb https://github.com/masc-ucsc/livehd >hercules3.data
    labours -f pb -m old-vs-new -o hercules3a.pdf <hercules3.data 
    labours -f pb -m devs -o hercules3b.pdf <hercules3.data
    labours -f pb -m devs-efforts -o hercules3c.pdf <hercules3.data

## Test/Developer LiveHD case (no commits)

If you do not plan to do many changes, and just wants to try LiveHD or be a
LiveHD user, the easiest way is to just clone the repo:

    git clone https://github.com/masc-ucsc/livehd

From time to time, you should get the latest version to have the latest bug fixes/patches.
Just a typical git pull should suffice:

    git pull

### Infrequent Contributor Flow

If you work outside UCSC and/or you are an infrequent contributor, you have two
main options: fork or private clone. The fork approach requires you to have
your repository public, if you have publications or work-in-progress that you
do not want to share the best option is to have a private repo (livehd-private).

The simplest way to contribute to LiveHD is to create a public repo or a public
fork, and a pull request. Most git guides use the origin/master (in fork or
private repo) to synchronize with upstream/master (upstream main LiveHD repo).
This means that your local changes should NOT go to your origin/master.
Instead, you should create a branch for your local work. This works like a
charm if you do pull requests, and it is reasonable if you have a long
development branch without intention to push upstream.


Although it is possible to create a different setup, we recommend that you keep
the origin/master clean to synchronize with upstream/origin. You should create
a new branch for each feature that you may want to upstream (origin/feature-x),
and a local development branch (dev) for all your team members.

1.a. To create a private repo:

    git clone --bare https://github.com/masc-ucsc/livehd.git livehd-upstream
    cd livehd-upstream.git
    git push --mirror git@github.com:XXX/livehd-private.git
    cd ..
    rm -rf livehd-upstream.git

1.b. To create a public fork repo:

You could do the same steps as the private repo, or just fork (
https://github.com/masc-ucsc/livehd/fork ) the LiveHD repo.

2. Create development branch (dev)

    git checkout -b dev

3. Create a branch from origin/master to create a pull request to upstream/master

    git checkout -b pull_request_xxx

4. Create a branch from dev for internal development if needed

    git checkout -b feature_xx_4dev dev

5. Synchronize origin/master from main upstream/master

    # Add remote upstream (if not added before)
    git remote -v   
    # If remote -v did not list upstream. Add them
    git remote add upstream https://github.com/masc-ucsc/livehd.git
    git fetch upstream

    # Make sure that you are in origin/master
    git checkout master

    # Bring the changes from the remote upstream/master to local master/origin
    git merge upstream/master

    # Push to repo origin/master if everything was fine
    git push origin master

    # To see the difference with upstream (it should be empty)
    git diff @{upstream}

6. Synchronize the dev branch with the latest master sync

    git checkout dev
    git merge origin/master
    git push  # same as "push origin dev" because dev is checkout

7. In case that you did not, push to the corresponding branch to the server

    git push origin dev
    git push origin pull_request_xxx
    git push origin feature_xx_4dev

8. Create new [pull][pull] request to upstream

    # Make sure that origin/master is in sync (step 5)
    git diff @{upstream} # should be empty

    # Rebase/merge the feature request with latest origin master
    git checkout pull_request_xxx
    git rebase master
    git push upstream pull_request_xxx

Now create a [pull][pull] request through github, and the UCSC/MASC team will review it.

## Frequent Contributor

If you are working on LiveHD at UC Santa Cruz, contact [Jose
Renau](http://users.soe.ucsc.edu/~renau/) to be added to the MASC organization
on GitHub so that you have write access to the repo. The setup is similar to
the [infrequent contributor flow](#infrequent-contributor-flow) but you have
access to directly commit to the public repository. Even the upstream/master.


[pull]: https://help.github.com/articles/creating-a-pull-request
[masc]: http://masc.soe.ucsc.edu/
