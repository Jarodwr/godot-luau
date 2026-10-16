;; defnode (ADR 0057): every body form, checked by fennel_smoke.gd
(local Features (require :res://features/derived.luau))
(local BONUS 10)                                   ; constants: plain locals

(defnode FennelNode Features
  (prop power 3 {})                                ; exported, type from the default
  (group Combat                                    ; an inspector section: all exported
    (prop health 50 {:type gd.int :range [0 100] :set :set_health :get :get_health})
    (subgroup "Shield Tuning"
      (prop shield 2)))
  (prop title "x" {})                              ; ungrouped, declared after a group
  (prop target nil {:type gd.Node})                  ; exported, no default
  (prop phase 1)                                   ; plain property
  (prop dead false {:export false :get :is_dead})  ; hidden, read-only
  (signal enraged)
  (signal hit [damage gd.int] by [boss Features])  ; a name, or [name type]

  (method set_health [v]
    (set self._health (math.max v 0)))

  (method get_health []
    (or self._health 50))

  (method is_dead []
    (= (or self._health 50) 0))

  (method describe []
    (.. "fennel+" (super describe)))               ; Features.describe(self)

  (method shout [who punct] {:defaults [:world :!] :args [gd.String gd.String] :ret gd.String}
    (.. "HEY " who punct))

  (method bonus []
    (+ BONUS self.power)))
