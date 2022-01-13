Continuous Integration
======================

This document provides describes the continuous integration setup for this
repository.

The main script that is executed is ``ci/build.sh`` and should contain all
necessary checks to be performed, although some of them might be run
conditionally.

lcitool
-------

GitLab CI does not provide large variety of platforms to run the builds on, but
there are couple of remedies for that. The setup used here leverages command
called ``lcitool`` which is provided by `libvirt-ci`_

The ``lcitool`` simplifies various repeating work that would otherwise need to
be repeated quite often and also abstracts away lot of platform specific details
like package installation etc.

For that to work properly the list of dependencies is part of libvirt-ci and it
also provides mapping for different naming conventions used by various
distributions.

In order to run ``lcitool`` it is enough to have the repository cloned locally,
the executable script is in the root directory of that repository.

Linux builds
------------

In order to run builds on various Linux distributions the CI uses containers to
widen the coverage, so the tests are run with GitLab CI's docker setup.
Container images are prepared during the build and cached, so that they do not
need to be rebuilt for every run, or even built from scratch when changed
slightly. The cached container images can be deleted at any time as they will
be correctly rebuilt in case of a cache miss. The Dockerfiles are generated
using ``lcitool`` and stored under ``ci/containers``. In order to update the
files one needs to run ``lcitool manifest ci/manifest.yml`` from the root of the
git repository, for example after an update to libvirt-ci (e.g. when adding a
new dependency).

Recreating the builds locally is pretty straightforward. Choose a file from
``ci/containers`` which represents the desired setup. In this example let's
pick Fedora rawhide.

In order to build your container image and tag it (optional, but easier to refer
to later on) we can utilise podman (or feel free to substitute "podman" with
"docker"):

.. code-block:: shell

    podman build -f ci/containers/fedora-rawhide.Dockerfile -t nbdkit-fedora-rawhide

That will get you a container image tagged ``nbdkit-fedora-rawhide`` that you
can execute the tests on. You can then start a container using the image, in
this case from the root of the repository:

.. code-block:: shell

    podman run -it --rm --userns=keep-id -v .:/repo:z -w /repo nbdkit-fedora-rawhide bash

or

.. code-block:: shell

    docker run -it --rm --user $UID:$UID -v $PWD:/repo:z -w /repo nbdkit-fedora-rawhide bash

which will bind-mount the current directory (root of the repository in our case)
onto /repo inside the container and also use that path as the working directory
(just so you do not have to ``cd /repo`` before any commands. This example
illustrates running bash, which can be used to debug any issues in the build,
but any command can be specified, for example the build script directly.  It
also runs the command under a user with the same UID as the user running the
command (even with the same UID).

Since the directory is bind-mounted any changes will be visible in your local
repository and vice versa. That is useful when you want to, for example, make a
change using your favourite editor, but already have some changes in the running
container.

To avoid common issues and replicate a clean build inside the container you
should clean everything or at least use a VPATH build.

FreeBSD and macOS
-----------------

GitLab CI does not offer shared runners for any of these platforms. To work
around this limitation, we take advantage of `Cirrus CI`_'s free offering: more
specifically, we use the `cirrus-run`_ script to trigger Cirrus CI jobs from
GitLab CI jobs so that the workaround is almost entirely transparent to users
and there's no need to constantly check two separate CI dashboards.

Reproducing these builds locally is not as straightforward as with the
containers. For FreeBSD you need to set up your own VM and there is no way to
run the macOS builds without the hardware.

You can, however have the CI running the build for your GitLab fork, although
there is a one-time setup required. If you want FreeBSD and macOS builds to
happen when you push to your GitLab repository, you need to

* set up a GitHub repository for the project, eg. ``yourusername/reponame``.
  This repository needs to exist for cirrus-run to work, but it doesn't need to
  be kept up to date, so you can create it and then forget about it;

* enable the `Cirrus CI GitHub app`_ for your GitHub account;

* sign up for Cirrus CI. It's enough to log into the website using your GitHub
  account;

* grab an API token from the `Cirrus CI settings`_ page;

* it may be necessary to push an empty ``.cirrus.yml`` file to your github fork
  for Cirrus CI to properly recognize the project. You can check whether
  Cirrus CI knows about your project by navigating to:

  ``https://cirrus-ci.com/yourusername/reponame``

* in the *CI/CD / Variables* section of the settings page for your GitLab
  repository, create two new variables:

  * ``CIRRUS_GITHUB_REPO``, containing the name of the GitHub repository
    created earlier, eg. ``yourusername/reponame``;

  * ``CIRRUS_API_TOKEN``, containing the Cirrus CI API token generated earlier.
    This variable **must** be marked as *Masked*, because anyone with knowledge
    of it can impersonate you as far as Cirrus CI is concerned.

  Neither of these variables should be marked as *Protected*, because in
  general you'll want to be able to trigger Cirrus CI builds from non-protected
  branches.

Once this one-time setup is complete, you can just keep pushing to your GitLab
repository as usual and you'll automatically get the additional CI coverage.

.. _libvirt-ci: https://gitlab.com/libvirt/libvirt-ci`_.
.. _Cirrus CI GitHub app: https://github.com/marketplace/cirrus-ci
.. _Cirrus CI settings: https://cirrus-ci.com/settings/profile/
.. _Cirrus CI: https://cirrus-ci.com/
.. _MinGW: http://mingw.org/
.. _cirrus-run: https://github.com/sio/cirrus-run/
