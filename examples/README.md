# Examples

Each example is a complete program, not a fragment. `make test` runs each
one and checks the output against the `.expected` file next to it. This
makes sure that the examples continue to work.

Every example makes the repository and the working copy that it uses under
`examples/tmp`. The examples do not need one of your repositories, an
installed svn, or a network. To run one example:

```
CLIPSVN -f2 examples/1-history-as-facts.bat
```

| | |
| --- | --- |
| [1-history-as-facts.bat](1-history-as-facts.bat) | Revisions and the paths they changed become facts, and rules make the conclusions |
| [2-rules-over-status.bat](2-rules-over-status.bat) | The status of the working copy as facts, and a rule that makes a commit |
| [3-diff-and-blame.bat](3-diff-and-blame.bat) | The person who wrote each line of a file, and the paths that differ between two revisions |
