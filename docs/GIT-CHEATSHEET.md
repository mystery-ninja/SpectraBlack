# Git Cheat Sheet

A reference card. Scan it, grab the command, get back to work.

If you want git *explained* rather than listed, read
[`10-GIT-GUIDE.md`](10-GIT-GUIDE.md) instead — it covers the mental model and
walks through why things work the way they do. This file assumes you just need
the command.

> **Numbered docs** (`00-` to `11-`) are the project plan, read in order.
> **Unnumbered docs** like this one are references, read whenever.

---

## 1. Do this first (once per machine)

Nothing else works properly until this is done.

```bash
# Who you are. This is stamped on every commit you make, permanently.
git config --global user.name  "Your Name"
git config --global user.email "you@example.com"
```

**Use your GitHub noreply email** if you do not want your real address public —
find it at GitHub → Settings → Emails. It looks like
`12345678+username@users.noreply.github.com`. Commit emails are visible to
anyone forever, so this matters.

```bash
# Sensible defaults
git config --global init.defaultBranch main
git config --global pull.ff only
git config --global alias.lg "log --oneline --graph --decorate --all"
```

Then get the project:

```bash
git clone https://github.com/mystery-ninja/SpectraBlack.git
cd SpectraBlack
```

Verify it worked:

```bash
git config --global --get user.email   # should print your email
git log --oneline -1                   # should print a commit
```

---

## 2. The daily loop

This is 90% of git. Learn these six and you can work.

| Command | What it does |
|---|---|
| `git status` | What has changed? **Run this constantly.** |
| `git pull` | Get everyone else's work |
| `git add <file>` | Mark a file for the next commit |
| `git add -A` | Mark *everything* changed |
| `git commit -m "msg"` | Save a snapshot of what you marked |
| `git push` | Send your commits to GitHub |

A normal session:

```bash
git pull                                  # start current
# ... edit files ...
git status                                # see what you changed
git add docs/BRAINSTORM.md
git commit -m "docs: ideas for particle binding"
git push
```

**`git status` is your friend.** When confused, run it. It tells you what branch
you are on, what is changed, and usually suggests the command you want.

---

## 3. Seeing what is going on

| Command | What it does |
|---|---|
| `git status` | Current state of your files |
| `git diff` | What you changed but have not staged |
| `git diff --staged` | What you staged but have not committed |
| `git log --oneline -10` | Last 10 commits, one line each |
| `git lg` | Pretty graph of all branches (alias from above) |
| `git show <commit>` | Everything about one commit |
| `git log -p <file>` | History of one file, with diffs |
| `git blame <file>` | Who last changed each line, and in which commit |

`git blame` sounds accusatory but is mostly used to find *the commit* that
introduced a line, so you can read its message and learn why the line exists.

---

## 4. Branches

A branch is just a label pointing at a commit. Creating one is instant and free,
so make one whenever you are unsure.

| Command | What it does |
|---|---|
| `git switch -c my-branch` | Create a branch and move to it |
| `git switch main` | Move to an existing branch |
| `git switch -` | Back to the previous branch |
| `git branch` | List local branches |
| `git push -u origin my-branch` | Publish a new branch (first push only) |
| `git branch -d my-branch` | Delete a branch you are done with |

Merging your branch into `main`:

```bash
git switch main
git pull
git merge my-branch
git push
git branch -d my-branch
```

---

## 5. Undo and rescue

The situations you will actually hit.

| Situation | Fix |
|---|---|
| Bad commit message, not pushed yet | `git commit --amend -m "better message"` |
| Forgot a file in the last commit | `git add f` then `git commit --amend --no-edit` |
| Undo last commit, keep the changes | `git reset --soft HEAD~1` |
| Unstage a file, keep the changes | `git restore --staged <file>` |
| Throw away changes to one file | `git restore <file>` ⚠️ unrecoverable |
| Need to switch branches mid-work | `git stash` → switch → `git stash pop` |
| Committed to the wrong branch | `git branch fix-here` → `git reset --hard origin/main` → `git switch fix-here` |
| **Everything is broken** | `git reflog` — see below |

### `git reflog` is the safety net

It logs every position you have been in, **including commits no longer reachable
from any branch**. Find the hash from before you broke things:

```bash
git reflog                      # find the good hash
git reset --hard <that-hash>
```

**If you committed it at any point, it is almost certainly recoverable.** That is
the reason to commit often, even imperfectly — committing is what makes work
safe.

---

## 6. Things to watch out for

### ⚠️ The one thing that genuinely destroys other people's work

```bash
git push --force        # DON'T, on a shared branch
```

Force-pushing overwrites the remote's history. If Schxma pushed something and you
force-push over it, **his commits are gone from the remote.** Everything else in
git is recoverable; this is the exception.

If you think you need `--force`, ask first.

### ⚠️ Destroys *your* uncommitted work, with no warning

| Command | What it kills |
|---|---|
| `git restore .` | All uncommitted edits |
| `git reset --hard` | All uncommitted edits |
| `git clean -fd` | All untracked files, deleted from disk |

None of these can be undone, because the work was never committed. Commit first
if there is any doubt.

### ⚠️ Commit emails are public forever

Covered in section 1. Use the noreply address.

### ⚠️ Never commit large binaries

Git keeps **every version of every file forever**. One 50 MB test video makes
every future clone 50 MB bigger, *even after you delete it*. Removing it properly
requires rewriting history, which is painful once a repo is public.

Our `.gitignore` already blocks audio and video files. If we ever genuinely need
binary test assets, we set up Git LFS **before** committing the first one.

### ⚠️ Don't commit secrets

Tokens, passwords, keys. Same problem as large files: deleting the file does not
remove it from history. If it happens, assume the secret is compromised and
rotate it immediately — do not just delete and commit.

### ⚠️ Commands to leave alone for now

| Command | Why |
|---|---|
| `git push --force` | Destroys other people's commits |
| `git rebase -i` | Genuinely useful, but rewrites history. Learn it later |
| `git reset --hard` | Silently deletes uncommitted work |
| `git clean -fd` | Permanently deletes untracked files |

Normal daily work needs none of these.

---

## 7. "Oh no" recipes

### My push was rejected — "fetch first"

**The most common thing you will hit on this project.** Someone else pushed while
you were working. Nothing is broken.

```bash
git pull
git push
```

Do **not** use `--force` here.

### I have a merge conflict

Git puts markers in the file:

```
<<<<<<< HEAD
your version
=======
their version
>>>>>>> their-branch
```

Edit the file so it says what it should, **delete all three marker lines**, then:

```bash
git add <file>
git commit
```

See which files are conflicted with `git status`. To bail out entirely:
`git merge --abort`.

### I cloned but my commits show the wrong name

You skipped section 1. Fix the config, then for commits already made:

```bash
git commit --amend --reset-author --no-edit     # fixes the most recent one only
```

### I want to see what someone else changed

```bash
git pull
git log --oneline -5
git show <their-commit-hash>
```

---

## 8. Our project workflow

**Flat and trust-based.** Everyone with access pushes directly, including to
`main`. No mandatory review, no protected branches.

- **Push straight to `main`** for docs, planning, small fixes.
- **Use a branch** when the change is big, spans multiple sittings, touches
  something someone else is working on, or when you want a second opinion.
- **Pull before you start, pull before you push.** This one habit prevents most
  friction.

### Commit messages

Format: `module: what changed`, imperative, under 72 characters.

```
good:   audio: cache FFT analysis to disk by file hash
good:   docs: add ideas on particle audio binding
good:   gpu: fix framebuffer leak on viewport resize

bad:    fixed stuff
bad:    WIP
bad:    Update README.md
```

Module prefixes we use: `core`, `gpu`, `audio`, `render`, `export`, `app`,
`docs`, `build`.

If a commit needs explaining, leave a blank line after the summary and write
prose. Explain **why**, not what — the diff already shows what.

### Branch naming

| Prefix | Use |
|---|---|
| `feature/` | New functionality |
| `fix/` | Bug fixes |
| `docs/` | Documentation only |
| `refactor/` | Restructuring, no behavior change |
| `perf/` | Performance work |

---

## If you remember only one thing

**Commit often, and pull before you push.**

Small frequent commits are cheap, recoverable, and nearly impossible to lose.
Every bad git situation comes from either a huge pile of uncommitted work, or
from rewriting history that other people already have.
