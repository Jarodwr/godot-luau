;; import-macros from res:// (docs/adr/0062): a dotted name, and a relative
;; one whose file imports another relatively. hot_reload.gd imports by a
;; res:// path, which fennel-ls doesn't resolve.
(import-macros {: twice : thrice : whoami} :macros.util)
(import-macros {: quadruple} "./local_macros")

(defnode FennelMacros gd.Node :global false
  (method run [x]
    [(twice x) (quadruple x) (thrice x)])
  (method whoami []
    (whoami)))
