;; Moves its node; fennel_smoke.gd checks the position (defnode: ADR 0057)
(defnode Mover gd.Node2D
  :global false

  (method _ready []
    (set self.velocity (gd.Vector2 1 0.5)))

  (method _process [delta]
    (set self.position (+ self.position (* self.velocity delta)))))
