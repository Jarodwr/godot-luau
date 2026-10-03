;; Per-frame work for the process_nodes case, in Fennel on Luau
(local Mover {:extends "Node2D"})

(fn Mover._ready [self]
  (set self.velocity (Vector2 1 0.5)))

(fn Mover._process [self delta]
  (set self.position (+ self.position (* self.velocity delta))))

Mover
