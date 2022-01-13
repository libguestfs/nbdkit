#!/bin/bash

set -e

build_container() {
    podman build -f "$1" -t "$2"
}

build_project() {
    echo podman run --rm -it -v .:/repo -w /repo "$1" bash -xc "$2"
    podman run --rm -it -v .:/repo -w /repo "$1" bash -xc "$2"
}

usage() {
    echo 'dunno yet'
}

main() {
    cd "$(git rev-parse --show-toplevel 2>/dev/null || echo ".")"

    arch="x86_64"
    while getopts "a:h" opt
    do
        case $opt in
            a)
                if [[ -z "$OPTARG" ]]
                then
                    echo "Empty architecture is invalid" >&2
                    exit 126
                fi
                arch="$OPTARG"
                ;;
            h)
                usage
                exit 0
                ;;
            '?')
                usage >&2
                exit 126
                ;;
        esac
    done

    shift $(($OPTIND-1))

    local distro="$1"
    if [[ -z "$distro" ]]
    then
        echo "No distro specified" >&2
        usage >&2
        exit 126
    fi

    local cmd="$2"
    if [[ -z "$2" ]]
    then
        if [[ -n "$(git clean -nXd)" ]]
        then
            echo "Tree is not clean, please run 'git clean -fXd'" >&2
            exit 126
        fi
        cmd="ci/build.sh"
    fi

    local project
    project="$(yq -r '.projects[0]' ci/manifest.yml)"
    if [[ -z "$project" ]]
    then
        echo "Could not parse project name from ci/manifest.yml" >&2
        exit 126
    fi

    local suffix
    if [[ "$arch" != "x86_64" ]]
    then
        suffix="-cross-$arch"
    fi
    local file="ci/containers/$distro$suffix.Dockerfile"

    local tag="ci-$project-$distro$suffix"
    echo "Testing $file as $tag"

    local status=0

    set +e
    build_container $file $tag
    status="$?"
    set -e
    if [[ "$status" -ne 0 ]]
    then
        echo "Container build failed with exit code $status" >&2
        exit $status
    fi

    local before_script
    before_script="$(yq -r '.[".base_build_job"].before_script[]' .gitlab-ci.yml)"

    local variables
    variables="$(yq -r '.["'"$arch-$distro"'"].variables | to_entries[] | "export " + .key + "=\"" + .value + "\""' ci/gitlab.yml)"

    set +e
    build_project $tag "$before_script; $variables; $cmd"
    status="$?"
    set -e
    if [[ "$status" -ne 0 ]]
    then
        echo "Project build failed with exit code $status" >&2
        exit $status
    fi
}

main "$@"
