# Raw Touch Provider v1

## Purpose

Touch is a physical input capability, not a UI gesture implementation. The physical
provider is the sole owner of the touch controller and the sole component permitted
to acknowledge controller event/status registers.

The first capability is `input.touch.raw` API v1, defined by
`sdk/driver/RiscTouchV1.h`.

## Contract

The provider publishes exact DOWN, MOVE and UP transitions with monotonically
increasing sequence numbers. Consumers also have an authoritative snapshot of active
contacts and native surface geometry. Event delivery is bounded: if a consumer falls
behind and its cursor overruns retained history, `next()` returns -1. The consumer
then discards local held-state, reads `snapshot()`, and resubscribes.

This dual event/snapshot model prevents a missed UP event from leaving applications
with a permanently held virtual button.

Subscriptions begin at the current tail by default. That prevents a touch initiated
under a previous UI or application from being replayed into a newly launched
consumer. `reset()` provides an explicit security/UI boundary and requires a fresh
physical DOWN before a new gesture can be recognized.

## Layering

```
physical controller driver (GT911, future USB touch, etc.)
    -> input.touch.raw
        -> gesture provider (tap / hold / swipe / double-tap)
        -> UI navigation projection
        -> game/direct-contact consumer
```

The generic runtime treats `input.touch.raw` as an opaque capability. Controller
registers, I2C transactions, IRQ handling, contact tracking, reset/recovery and event
acknowledgement remain in the physical provider.

## Coordinate policy

The raw provider reports the touch surface's native logical coordinate space plus
width and height. Display rotation and application-specific layout transforms belong
above the physical provider. This keeps calibration and physical-device semantics
stable while allowing portrait, landscape and rotated applications to project the
same contact stream differently.
