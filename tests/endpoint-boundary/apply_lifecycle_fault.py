#!/usr/bin/env python3
"""Prepare one lifecycle fault in a distinct disposable container owner copy."""

import argparse
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def replace_function(source, name, change):
    found = re.search(
        r"^static\s+(?:int|void)\s+" + re.escape(name) + r"\(", source, re.M
    )
    if found is None:
        raise ValueError("missing production function: " + name)
    opening = source.index("{", found.end())
    depth = 0
    for token in re.finditer(
        r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
        source[opening:],
        re.S,
    ):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                end = opening + token.end()
                before = source[found.start() : end]
                after = change(before)
                if before == after:
                    raise ValueError("selected fault changed nothing")
                return source[: found.start()] + after + source[end:]
    raise ValueError("unterminated production function")


def remove_cancel(body):
    if body.count("cancel_all_motion_locked(data);") != 1:
        raise ValueError("expected exactly one lifecycle cancellation")
    return body.replace("cancel_all_motion_locked(data);", "")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "fault",
        choices=(
            "endpoint-retirement",
            "activity-retirement",
            "endpoint-admission",
            "noop-retirement",
            "first-stream-only",
        ),
    )
    parser.add_argument("isolated_owner", type=Path)
    args = parser.parse_args()
    target = args.isolated_owner.resolve()
    if target == ROOT or ROOT.is_relative_to(target):
        parser.error("refusing to modify the operator's owner checkout")
    path = target / "src/input_processor_inertia.c"
    if not path.resolve().is_relative_to(target):
        parser.error("refusing a source link outside the disposable owner")
    source = path.read_text()
    if args.fault == "endpoint-retirement":
        source = replace_function(source, "sync_endpoint_locked", remove_cancel)
    elif args.fault == "activity-retirement":
        source = replace_function(source, "inertia_lifecycle_event", remove_cancel)
    elif args.fault == "endpoint-admission":

        def remove_admission(body):
            statement = "sync_endpoint_locked(data, zmk_endpoint_get_selected());"
            if body.count(statement) != 2:
                raise ValueError(
                    "expected admission and completion endpoint observations"
                )
            return body.replace(statement, "", 1)

        source = replace_function(source, "inertia_input_callback", remove_admission)
    elif args.fault == "noop-retirement":

        def cancel_noop(body):
            statement = "sync_endpoint_locked(data, endpoint->endpoint);"
            if body.count(statement) != 1:
                raise ValueError("expected one endpoint event observation")
            return body.replace(
                statement, statement + "\n        cancel_all_motion_locked(data);"
            )

        source = replace_function(source, "inertia_lifecycle_event", cancel_noop)
    else:

        def first_stream(body):
            changed, count = re.subn(
                r"\bi\s*<\s*INERTIA_STREAM_COUNT\b", "i < 1U", body
            )
            if count != 1:
                raise ValueError("expected one complete stream loop")
            return changed

        source = replace_function(source, "cancel_all_motion_locked", first_stream)
    path.write_text(source)
    print("Applied lifecycle fault: " + args.fault)


if __name__ == "__main__":
    main()
