# LikeC4 Element Kind Naming

## Problem

The `subsystem` kind classifies elements by their depth beneath a parent rather
than by what sort of architectural thing they are. That information is already
visible through nesting, while the boundary between `subsystem` and `component`
depends on subjective size.

## Proposed Approach

Remove `subsystem` instead of replacing it with a synonym. Classify its current
elements as `component`; retain specific kinds such as `interface` and `entity`
only where they communicate a distinct architectural nature. Use titles,
descriptions, relationships, and nesting to show each component's role and
scope. Do not use `container`: C4 reserves that term for an independently
running application or data store, while these elements execute inside the
firmware runtime.

## Open Questions

- None.

## Decisions Log

- 2026-07-31: Replace `subsystem` with `component` for all five current
  elements - nesting already communicates depth, while `component` describes
  their architectural nature.
- 2026-07-31: Do not use `container` - C4 reserves it for independently running
  applications and data stores, which these in-process elements are not.
