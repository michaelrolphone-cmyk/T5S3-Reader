#!/usr/bin/env python3
"""Regression checks for HalFile lifetime synchronization."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "lib/hal/HalStorage.cpp").read_text(encoding="utf-8")


def between(start: str, end: str) -> str:
    begin = SOURCE.index(start)
    finish = SOURCE.index(end, begin + len(start))
    return SOURCE[begin:finish]


class HalFileLifetimeContract(unittest.TestCase):
    def test_destructor_destroys_raw_fsfile_under_storage_lock(self):
        body = between("HalFile::~HalFile()", "HalFile::HalFile(HalFile&&)")
        self.assertNotIn("= default", body)
        lock = body.index("HalStorage::StorageLock lock;")
        destroy = body.index("impl.reset();")
        self.assertLess(lock, destroy)

    def test_move_assignment_destroys_previous_raw_fsfile_under_storage_lock(self):
        body = between(
            "HalFile& HalFile::operator=(HalFile&& other)",
            "HalFile HalStorage::open(",
        )
        self.assertNotIn("= default", body)
        lock = body.index("HalStorage::StorageLock lock;")
        destroy = body.index("impl.reset();")
        self.assertLess(lock, destroy)
        self.assertIn("impl = std::move(other.impl);", body)

    def test_open_helpers_release_outer_lock_before_halfile_assignment(self):
        for name, next_name in (
            ("bool HalStorage::openFileForRead(", "bool HalStorage::openFileForRead(const char* moduleName, const std::string&"),
            ("bool HalStorage::openFileForWrite(", "bool HalStorage::openFileForWrite(const char* moduleName, const std::string&"),
        ):
            body = between(name, next_name)
            lock = body.index("StorageLock lock;")
            lock_scope_end = body.index("}", lock)
            assign = body.index("file = HalFile(")
            self.assertLess(lock_scope_end, assign)


if __name__ == "__main__":
    unittest.main()
