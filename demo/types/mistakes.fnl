;; Each marked line has one mistake that fennel-ls must report inside a
;; defnode (tools/check_lsp.sh); the rest must check clean
(local Base (require :res://features/derived.luau))
(local unused-local 3)                       ; never used

(defnode Mistakes Base
  :global false
  (prop speed (* SPEDE 2) {})                ; a typo in a default
  (group Combat
    (prop armor (+ ARMR 1)))                 ; a typo in a nested default
  (signal hit [target Bsae] by)              ; a typo in a signal type
  (alias label "Label")
  (method take [damage spare]                ; spare is never used
    (super take damage)
    (print (+ damage helth) by)))            ; a typo, and a signal argument used as a variable
