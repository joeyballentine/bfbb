# patches/

**Empty, and meant to stay that way.**

Local changes to `third_party/librw` used to live here as patch files, because
the submodule pointed at upstream `aap/librw` and there was nowhere to commit
them. That is no longer true: the submodule points at
`joeyballentine/librw`, branch `bfbb-port`, and changes go there as ordinary
commits.

The fork keeps `aap/librw` as its `upstream` remote, so rebasing onto new
upstream work stays a normal git operation, and anything general enough to
offer back can be a pull request from a branch of its own.

If a patch file ever reappears here, it means someone could not push to the
fork, and the right fix is to restore that access rather than to accumulate
patches -- a checkout with the submodule at the wrong commit fails to COMPILE
rather than misbehaving at run time, which is the right way round, but it is
still a trap for anyone cloning.
