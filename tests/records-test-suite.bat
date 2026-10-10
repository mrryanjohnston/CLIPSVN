; ======================================================================
; Records: the lists, and how they become facts and instances.
; ======================================================================

(println crlf "records-test-suite")

(defglobal ?*rec-repos* = (make-repos "records"))

(defglobal ?*rec-wc* = (make-wc "records" "records-wc"))

(deffunction records-test-suite ()
  (svn-client-set-auth ?*ctx* "ada")
  (commit-file ?*rec-wc* "a.txt" "a" "add a")
  (svn-client-set-auth ?*ctx* "tester")
  ; The root of the working copy is still at revision 0 after a commit, and a
  ; log of it starts there. The update brings it to 1.
  (svn-client-update ?*ctx* ?*rec-wc*)
  (write-file (str-cat ?*rec-wc* "/b.txt") "b")
  (write-file (str-cat ?*rec-wc* "/a.txt") "changed")

  (bind ?status (svn-client-status ?*ctx* ?*rec-wc*))


  (expect "svn-records-kind" status (svn-records-kind ?status))
  (expect "svn-records-count" 2 (svn-records-count ?status))
  (expect "svn-records-fields starts with path" path (nth$ 1 (svn-records-fields ?status)))
  (expect "svn-record-byindex has one value for each field"
          (length$ (svn-records-fields ?status)) (length$ (svn-record-byindex ?status 0)))
  (expect "in the order of the fields" "a.txt" (nth$ 1 (svn-record-byindex ?status 0)))
  (expect "svn-record-field by name" svn_wc_status_modified (svn-record-field ?status 0 node-status))
  (expect "and by string" svn_wc_status_unversioned (svn-record-field ?status 1 "node-status"))
  (expect "a value that is not set is nil" nil (svn-record-field ?status 1 changed-author))
  (bind ?r (svn-record-field ?status 0 colour))
  (expect "a field that is not there is a refusal" FALSE ?r)
  (bind ?r (svn-record-byindex ?status 2))
  (expect "an index past the end is FALSE, without a message" FALSE ?r)
  (bind ?r (svn-record-field ?status 2 path))
  (expect "for svn-record-field too" FALSE ?r)

  ; facts
  (bind ?fact (svn-record-to-fact ?status 0 change))
  (expect-true "svn-record-to-fact asserts a fact" (factp ?fact))
  (expect "with the path" "a.txt" (fact-slot-value ?fact path))
  (expect "the status" svn_wc_status_modified (fact-slot-value ?fact node-status))
  (expect "and the author" "ada" (fact-slot-value ?fact changed-author))
  (bind ?again (svn-record-to-fact ?status 0 change))
  (expect "the same fact again is the fact that exists" ?fact ?again)
  (bind ?r (svn-record-to-fact ?status 0 change-wrong-type))
  (expect "and so is a slot of the wrong type" FALSE ?r)
  (bind ?r (svn-record-to-fact ?status 0 no-such-template))
  (expect "and a template that is not there" FALSE ?r)
  (retract ?fact)
  (expect "svn-records-to-facts asserts one for each" 2 (svn-records-to-facts ?status change))
  (expect "and counts only the new ones the second time" 0 (svn-records-to-facts ?status change))
  (bind ?n 0)
  (do-for-all-facts ((?c change)) TRUE (bind ?n (+ ?n 1)))
  (expect "so there are two" 2 ?n)
  (do-for-all-facts ((?c change)) TRUE (retract ?c))

  ; a multislot takes the value as one element, or none for nil
  (bind ?log (svn-client-log ?*ctx* ?*rec-wc*))
  (bind ?fact (svn-record-to-fact ?log 0 log-entry))
  (expect "a multislot gets the value as one element" (create$ "ada") (fact-slot-value ?fact author))
  (expect "and a slot with no field is left alone" none (fact-slot-value ?fact tag))
  (retract ?fact)
  (bind ?changes (svn-log-changed-paths ?log))
  (expect "svn-records-to-facts of the changed paths" 1 (svn-records-to-facts ?changes changed-path))
  (bind ?fact (nth$ 1 (find-all-facts ((?c changed-path)) TRUE)))
  (expect "with nil for a copyfrom that is not there" nil (fact-slot-value ?fact copyfrom-path))
  (retract ?fact)
  (svn-records-free ?log)

  ; instances
  (bind ?instance (svn-record-to-instance ?status 0 CHANGE))
  (expect-true "svn-record-to-instance makes an instance" (instance-existp ?instance))
  (expect "with the path" "a.txt" (send ?instance get-path))
  (expect "and a multislot as one element" (create$ svn_node_file) (send ?instance get-kind))
  (send ?instance delete)
  (bind ?instance (svn-record-to-instance ?status 1 CHANGE "second"))
  (expect "and with a name" [second] (instance-name ?instance))
  (send ?instance delete)
  (bind ?r (svn-record-to-instance ?status 0 NO-SUCH-CLASS))
  (expect "a class that is not there is a refusal" FALSE ?r)
  (expect "svn-records-to-instances makes one for each" 2 (svn-records-to-instances ?status CHANGE))
  (bind ?n 0)
  (do-for-all-instances ((?c CHANGE)) TRUE (bind ?n (+ ?n 1)))
  (expect "so there are two" 2 ?n)
  (do-for-all-instances ((?c CHANGE)) TRUE (send ?c delete))

  (expect "svn-records-free frees" TRUE (svn-records-free ?status))
  (bind ?r (svn-records-count ?status))
  (expect "after which the list is refused" FALSE ?r)
  (svn-client-revert ?*ctx* ?*rec-wc* infinity)
  (svn-repos-free ?*rec-repos*))

(records-test-suite)
