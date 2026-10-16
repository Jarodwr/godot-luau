;; A default is evaluated for each instance (ADR 0060); fennel_smoke.gd
;; changes one instance's values and reads another's
(defnode FennelFreshDefaults gd.Node
  :global false
  (prop items [])
  (prop tags {})                           ; a map: a Dictionary
  (prop nested [[1]])
  (prop curve (gd.Curve.new) {})
  (prop speed 1.5 {}))
