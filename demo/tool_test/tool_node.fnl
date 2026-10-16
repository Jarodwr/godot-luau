;; Runs in the editor (deftool, ADR 0057)
(deftool ToolNode gd.Node
  :global false
  (prop size 3 {})

  (method _ready []
    (print (.. "FENNEL TOOL READY editor=" (tostring (gd.Engine:is_editor_hint)) " size=" (tostring self.size)))))
