# 10 — Git Guide

Written for you specifically, since you mentioned being rusty. The first part is
a refresher on the mental model, the second is our actual workflow, and the third
is how to get out of trouble.

## The mental model

Git has three places a file can be, and almost all confusion comes from not
knowing which one you are looking at.

```
  working directory  →  staging area  →  repository
  (your actual files)    (git add)        (git commit)
```

1. **Working directory** — the files on disk, as you edited them.
2. **Staging area** ("the index") — changes you have marked as belonging in the
   next commit. This exists so you can commit *some* of your changes, not all.
3. **Repository** — the permanent history of commits.

Then there is a fourth place: **the remote** (GitHub), which you sync with using
`push` and `pull`.

A **commit** is a snapshot of the whole project plus a message. A **branch** is
just a movable label pointing at a commit. That is genuinely all a branch is,
which is why creating one is instant and free.

## The daily loop

Ninety percent of git usage is these five commands:

```bash
git status                 # what has changed? run this constantly
git add <file>             # stage a file  (git add -p for chunks of a file)
git commit -m "message"    # snapshot the staged changes
git pull                   # get other people's work
git push                   # send yours
```

`git status` is your friend. When you are unsure what is going on, run it. It
tells you what branch you are on, what is staged, what is not, and usually
suggests the command you want.

## Setup, one time

```bash
git config --global user.name  "Your Name"
git config --global user.email "you@example.com"

# Make 'main' the default branch name for new repos
git config --global init.defaultBranch main

# Only fast-forward or create a merge commit on pull — avoids surprise rebases
git config --global pull.ff only

# Nicer log output, as `git lg`
git config --global alias.lg "log --oneline --graph --decorate --all"
```

For pushing to GitHub you will want SSH keys so you are not typing a password
constantly:

```bash
ssh-keygen -t ed25519 -C "you@example.com"
cat ~/.ssh/id_ed25519.pub     # paste this into GitHub → Settings → SSH keys
ssh -T git@github.com         # verify
```

## Creating this repository

```bash
cd ~/SpectraBlack
git init
git add .
git commit -m "Initial planning documentation"
```

Then make an empty repo on GitHub (no README, no .gitignore — we have ours
already) and connect it:

```bash
git remote add origin git@github.com:<you>/<reponame>.git
git branch -M main
git push -u origin main
```

The `-u` sets the upstream so future pushes are just `git push`.

## Our workflow

Since other people will be contributing, we use **branch per change, reviewed via
pull request**. Nobody commits directly to `main`, including you — not because
your commits are suspect, but because a protected `main` is what lets CI
guarantee that `main` always builds.

### Making a change

```bash
git switch main
git pull                                  # start from current main
git switch -c feature/spectrum-bars       # create and switch to a branch

# ... do the work, committing as you go ...
git add core/src/property.cpp
git commit -m "core: add driver blend operators"

git push -u origin feature/spectrum-bars  # publish the branch
```

Then open a pull request on GitHub, get a review, and merge.

### Branch naming

| Prefix | Use |
|---|---|
| `feature/` | New functionality |
| `fix/` | Bug fixes |
| `docs/` | Documentation only |
| `refactor/` | Restructuring with no behavior change |
| `perf/` | Performance work |

### Commit messages

Format: `module: what changed`, imperative mood, under 72 characters.

```
good:  audio: cache FFT analysis to disk by file hash
good:  gpu: fix framebuffer leak when resizing viewport
good:  docs: clarify preview/export parity requirement

bad:   fixed stuff
bad:   WIP
bad:   Update property.cpp
```

The module prefix matters in a multi-module project — it makes `git log --oneline`
scannable and tells a reviewer which subsystem to think about before they read a
word of the diff.

If a commit needs explanation, put a blank line after the summary and write
prose. Explain **why**, not what; the diff already shows what.

## Reading history

```bash
git lg                      # the graph (alias set up above)
git log --oneline -20       # last 20 commits
git log -p <file>           # history of one file, with diffs
git blame <file>            # who last touched each line, and in which commit
git show <commit>           # everything about one commit
git diff                    # unstaged changes
git diff --staged           # staged changes
git diff main...HEAD        # everything on this branch vs main
```

`git blame` sounds accusatory but is mostly used to find *the commit* that
introduced a line so you can read its message and understand why the line exists.

## Getting out of trouble

The situations you will actually hit, and the way out of each.

### I committed but want to change the message

```bash
git commit --amend -m "better message"
```

Only do this before pushing. Amending a pushed commit rewrites history and
requires a force push, which disrupts anyone who pulled it.

### I forgot to include a file in the last commit

```bash
git add forgotten.cpp
git commit --amend --no-edit
```

### I want to undo the last commit but keep my changes

```bash
git reset --soft HEAD~1     # commit undone, changes back in staging
```

### I want to throw away all my uncommitted changes

```bash
git restore .                      # discard working directory changes
git restore --staged <file>        # unstage without discarding
```

`git restore .` is destructive and unrecoverable. It is the one command in this
document to be careful with.

### I need to switch branches but am mid-work

```bash
git stash                   # shelve changes
git switch other-branch
# ... do something else ...
git switch -                # back to previous branch
git stash pop               # restore the shelved changes
```

### I have a merge conflict

Git marks the conflicting regions in the file:

```
<<<<<<< HEAD
your version
=======
their version
>>>>>>> their-branch
```

Edit the file so it contains what it should — deleting the three marker lines —
then:

```bash
git add <file>
git commit            # or `git merge --continue`
```

To see which files are conflicted: `git status`. To give up on the merge
entirely: `git merge --abort`.

### I committed to main by mistake

```bash
git branch feature/my-work        # save the work on a new branch
git reset --hard origin/main      # move main back to where the remote is
git switch feature/my-work        # continue there
```

### I have truly broken something

```bash
git reflog
```

This is the safety net and the single most valuable recovery command in git. It
logs every position `HEAD` has been in, including commits that are no longer
reachable from any branch. Find the hash from before you broke things:

```bash
git reset --hard <hash-from-reflog>
```

**Almost nothing committed to git is ever truly lost.** If you have committed at
some point, `reflog` can get it back. That is the reason to commit often, even
imperfectly — a commit makes work recoverable.

## Repository hygiene for this project

### Never commit

- Build output (`build/`, `*.o`, `*.so`, executables)
- IDE config (`.vscode/`, `.idea/`) beyond shared settings
- Large binary media — test audio and video belong in releases or Git LFS
- Analysis caches
- Anything from `~/AeNux` or the Avee RE folder

The `.gitignore` at the repo root covers these.

### On large files

Git stores every version of every file forever. One committed 50 MB test video
makes every future clone 50 MB heavier even after deletion. If we need binary
test assets, we use **Git LFS**, set up before the first such file is committed —
retroactively removing large files from history requires rewriting it, which is
painful on a public repo.

## Commands to avoid until you are comfortable

| Command | Why |
|---|---|
| `git push --force` | Overwrites remote history and other people's work. Use `--force-with-lease` if you must |
| `git rebase -i` | Powerful and genuinely useful, but it rewrites history. Learn it later |
| `git reset --hard` | Destroys uncommitted work with no warning |
| `git clean -fd` | Deletes untracked files permanently |

Normal daily work needs none of these.

## If you remember only one thing

**Commit often, on a branch, and push.** Small frequent commits on a branch are
cheap, recoverable, reviewable, and nearly impossible to lose. Every bad git
situation comes from either a huge uncommitted pile of work or from rewriting
history that other people already have.
