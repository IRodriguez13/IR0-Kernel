<!-- IR0 AI dev rule: ir0-git-commit-hygiene -->
<!-- alwaysApply: true -->
<!-- description: Git commits — maintainer identity only; Signed-off-by; never Co-authored-by -->

# Git commit hygiene

This rule is **always on**, including Cursor Cloud / background agents.
It is not optional documentation. Treat it like architecture rigor.

## Identity (mandatory)

Every commit the agent creates, amends, or rewrites **must** have
**Author** and **Committer** set to the maintainer only:

- **Name:** Iván Ezequiel Rodriguez
- **Email:** ivanrwcm25@gmail.com

Never publish `Author: Cursor Agent`, `cursoragent@cursor.com`, or any
tool/bot identity. Cloud platforms that default to an agent author **must**
be overridden (`GIT_AUTHOR_*` / `GIT_COMMITTER_*` / `git commit-tree`).

Always include DCO:

```text
Signed-off-by: Iván Ezequiel Rodriguez <ivanrwcm25@gmail.com>
```

Use `git commit -s` or `python3 scripts/ir0_git_commit.py` (preferred:
`commit-tree`, no IDE commit hooks).

## Forbidden: Co-authored-by (anyone)

**Never** add `Co-authored-by:` to a commit message or GitHub PR.

Forbidden in full, not only agent names:

- `Co-authored-by: Cursor …`
- `Co-authored-by: Cursor Agent <cursoragent@cursor.com>`
- `Co-authored-by: Iván Ezequiel Rodriguez <…>` (maintainer is the
  **author**, never a listed co-author of an agent commit)
- Any other named person or tool as co-author unless the user **explicitly**
  asks to add that person on that commit

GitHub shows PR co-authors from these trailers. Cloud agents that inject
`Author: <agent>` plus `Co-authored-by: <maintainer>` must **rewrite before
push** (`git commit-tree` / `git filter-repo`). Do not leave that pair on
published history.

Also do not add `Helped-by:`, `Reviewed-by:`, or similar attribution unless
the user explicitly asks.

## When creating commits

```bash
python3 scripts/ir0_git_commit.py -m "$(cat <<'EOF'
Short subject line.

Optional body in complete sentences.
EOF
)"
```

Or a plain shell with identity forced:

```bash
export GIT_AUTHOR_NAME='Iván Ezequiel Rodriguez'
export GIT_AUTHOR_EMAIL='ivanrwcm25@gmail.com'
export GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME"
export GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"
git commit -s --author="$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>" -m "$(cat <<'EOF'
Short subject line.

Optional body in complete sentences.
EOF
)"
```

Then **immediately** inspect `git log -1 --format='Author: %an <%ae>%n%b'`.
If `Co-authored-by:` or a non-maintainer author appeared, **do not push**.
Rewrite with `scripts/ir0_git_commit.py --amend-head` or `git commit-tree`.

- Plain HEREDOC title + body — no agent trailers.
- Do not append Co-authored-by “for courtesy” or tool “best practice”.

## Pre-push check

Before any push the agent initiates:

```bash
python3 scripts/repo_hygiene_guard.py
git log origin/master..HEAD --format='Author: %an <%ae>%n%b'
```

`repo_hygiene_guard.py` checks **this branch vs `origin/master`**. That catches
new cloud-agent commits. Use `python3 scripts/repo_hygiene_guard.py --all-history`
to audit commits already on master (requires an admin rewrite of protected
`master` to erase already-merged trailers).

Reject the push if any commit in the range has `Co-authored-by:` or an
author/committer other than Iván Ezequiel Rodriguez.
