# 0001. Record architecture decisions

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Design choices in this project are made after profiling and are easy to
forget or undo by accident. The reasons behind them (measurements, Godot and
Luau limitations) aren't visible in the code.

## Decision

We will record each significant design decision as a short, numbered ADR in
this folder, following [`template.md`](template.md). Proposed changes start as
`Proposed` and become `Accepted` when they land, with their measured effect.

## Consequences

- Decisions and their numbers are findable later.
- An accepted ADR isn't rewritten; a changed decision gets a new ADR that
  supersedes the old one.
