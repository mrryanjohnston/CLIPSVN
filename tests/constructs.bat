; ------------------------------------------------------------
; The templates and classes that the suites map records to.
;
; svn-record-to-fact fills the slots of a deftemplate that have the names of
; the fields of the record: path, node-status, revision, author, and the
; others that svn-records-fields lists. The other slots are not changed. So
; a template with only some of the names is correct. A multislot with the
; name of a field gets the value as a one-element multifield, or an empty one
; for nil.
; ------------------------------------------------------------

; the status of a path, with the fields a rule usually needs
(deftemplate change
  (slot path) (slot node-status) (slot text-status) (slot prop-status)
  (slot kind) (slot revision) (slot changed-author (default nil)))

; a log entry, with a multislot for a scalar field and an extra slot that is
; not changed
(deftemplate log-entry
  (slot revision) (multislot author) (slot date) (slot time) (slot message)
  (slot changed-path-count) (slot tag (default none)))

; a changed path of a revision
(deftemplate changed-path
  (slot revision) (slot action) (slot path) (slot kind) (slot copyfrom-path) (slot copyfrom-rev))

; a directory entry
(deftemplate entry (slot path) (slot kind) (slot size) (slot created-rev) (slot author))

; one line of a blame
(deftemplate line (slot line-number) (slot revision) (slot author) (slot line))

; a slot with the correct name but the wrong type, which causes a rejected
; call
(deftemplate change-wrong-type (slot path (type INTEGER)))

(defclass CHANGE (is-a USER) (role concrete)
  (slot path) (slot node-status) (multislot kind))

(defclass LOG-ENTRY (is-a USER) (role concrete)
  (slot revision) (slot author) (slot message))
