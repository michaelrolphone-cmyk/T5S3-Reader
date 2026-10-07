#!/usr/bin/env python3
"""Retired internal-flash X4 driver-store runner. No device access is supported.

The owner's SD-resident ordinary-driver correction supersedes ec0c099. Keep a
fail-closed entrypoint so old command lines cannot silently provision that
obsolete layout. Historical results remain in Git history; they do not qualify
the schema-2 SD handoff. Use x4_deployment_plan.py for read-only bundle checking.
"""
import json

RETIRED = ('Internal-flash X4 driver-store deployment is retired. Validate the '
           'schema-2 SD bundle, stage the offline SD card separately, and obtain '
           'separate authorization for any app-only device operation.')


def prepare(*args, **kwargs):
    raise ValueError(RETIRED)


def cycle(*args, **kwargs):
    # Retain the old callable only to reject historical importers before their
    # transport can be touched. No artifact or approval flag can revive it.
    raise ValueError(RETIRED)


def main():
    print(json.dumps({'result':'refused','reason':RETIRED,
                      'hardware_access_performed':False,'write_attempted':False}))
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
