(local T {:extends "Node"})

(fn T.boom [self]
  (let [t {}]
	(+ t.missing 1)))

T
