;; alias (ADR 0057): a child node looked up on first use; fennel_smoke.gd
;; checks it
(defnode FennelAlias gd.Node
  :global false
  (alias child "Child")
  (alias deep "Child/Grandchild")

  (method cached []                ; after the first read, a plain field
    (= (rawget self :child) self.child)))
