;; a default runs before the instance exists (ADR 0060)
(defnode DefaultSelf gd.Node
  :global false
  (prop doubled (* self.base 2) {}))
