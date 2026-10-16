;; import-macros of a file that isn't there

(import-macros {: nothing} :macros.missing)
(defnode MacrosMissing gd.Node :global false)
