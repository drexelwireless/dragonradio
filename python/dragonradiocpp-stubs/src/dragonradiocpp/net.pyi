from __future__ import annotations
import typing
__all__ = ['addRoute', 'addStaticARPEntry', 'deleteARPEntry', 'deleteRoute']
def addRoute(dest: str, mask: str, gateway: str) -> None:
    """
    Add an IP route
    """
def addStaticARPEntry(device: str | None, ip: str, mac: str) -> None:
    """
    Add a static ARP table entry
    """
def deleteARPEntry(device: str | None, ip: str) -> None:
    """
    Delete an ARP table entry
    """
def deleteRoute(dest: str, mask: str) -> None:
    """
    Delete an IP route
    """
