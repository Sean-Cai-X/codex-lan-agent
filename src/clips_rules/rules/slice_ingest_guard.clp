; Slice ingest quality/dedup rules.

(defrule duplicate-slice-route-canonical
  (declare (salience 60))
  (slice_ingest_fact (dedup_status "duplicate")
                     (canonical_slice_id ?canonical&:(neq ?canonical "")))
  =>
  (assert (clips_decision
    (domain "slice_ingest_guard")
    (target "dialog_slice")
    (decision "route")
    (verification "verified")
    (reason_code "duplicate_slice_merge")
    (route_target ?canonical)
    (next_action "merge into canonical slice instead of writing a new duplicate")
    (matched_rule "duplicate-slice-route-canonical"))))

(defrule duplicate-slice-block-status
  (declare (salience 55))
  (slice_ingest_fact (dedup_status "duplicate"))
  =>
  (assert (clips_decision
    (domain "slice_ingest_guard")
    (target "dialog_slice")
    (decision "block")
    (verification "not_verified")
    (reason_code "duplicate_slice_rejected")
    (next_action "reuse canonical_slice_id or set dedup_status to merged before retry")
    (matched_rule "duplicate-slice-block-status"))))

(defrule default-slice-ingest-allow
  (declare (salience -100))
  (slice_ingest_fact)
  (not (clips_decision (domain "slice_ingest_guard")))
  =>
  (assert (clips_decision
    (domain "slice_ingest_guard")
    (target "dialog_slice")
    (decision "allow")
    (verification "verified")
    (matched_rule "default-slice-ingest-allow"))))
