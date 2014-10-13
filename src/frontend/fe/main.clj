;; Copyright 2014 Google Inc. All rights reserved.
;;
;; Licensed under the Apache License, Version 2.0 (the "License");
;; you may not use this file except in compliance with the License.
;; You may obtain a copy of the License at
;;
;;     http://www.apache.org/licenses/LICENSE-2.0
;;
;; Unless required by applicable law or agreed to in writing, software
;; distributed under the License is distributed on an "AS IS" BASIS,
;; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
;; See the License for the specific language governing permissions and
;; limitations under the License.

(ns fe.main
  (:require [clojure.string :as s]
            [fe.compiler :as c])
  (:gen-class))

(defn parse-args [args]
  (def arg-config
    [{:long "--ir" :short "-i" :key :ir :value true}
     {:long "--output" :short "-o" :key :output :value true}
     {:long "--help" :short "-h" :key :help :value false}
     {:long "--version" :short "-v" :key :version :value false}])
  (defn find-config [flag]
    (loop [args arg-config]
      (cond (empty? args) nil
            (= flag (:long (first args))) (first args)
            (= flag (:short (first args))) (first args)
            :else (recur (rest args)))))

  (loop [remaining args
         options {}
         nonoptions []]
    (if (empty? remaining) (assoc options :other nonoptions)
      (let [flag (first remaining)
            value (if (> (count remaining) 1) (second remaining) nil)
            c (find-config flag)]
        (if (nil? c)
          ; flag not found: if starts with -, then error, else non-flag arg.
          (if (.startsWith flag "-") {:error (format "Unknown flag %s" flag)}
            (recur (drop 1 remaining) options (conj nonoptions flag)))
          ; flag found: requires value?
          (if (:value c)
            ; yes, requires value: consume flag and value; error if no value present.
            (if (nil? value)
              {:error (format "Missing value for flag %s" flag)}
              (recur (drop 2 remaining) (assoc options (:key c) value) nonoptions))
            ; no, does not require value: consume flag.
            (recur (drop 1 remaining) (assoc options (:key c) true) nonoptions)))))))

(def version-message
"autopiper frontend version 0.1.

autopiper is authored by Chris Fallin <cfallin@c1f.net>.


autopiper is Copyright (c) 2014 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the \"License\");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an \"AS IS\" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

autopiper is not an official Google product; it is a side project
developed independently by the author.
")

(def help-message
"Usage: autopiper [flags] <input>

    Flags:
        -o, --output <filename>:      specify Verilog output filename.
        -i, --ir <filename>:          specify IR output filename and
                                      compile to IR only.
        -h, --help:                   show this message.
        -v, --version:                show version information.
")


(defn show-error [errmsg]
  (binding [*out* *err*]
    (println errmsg))
  (System/exit 1))
(defn show-version []
  (show-error version-message))
(defn show-help []
  (show-error help-message))

(defn -main [ & args ]
  (let [args (parse-args args)]
    (cond
      (empty? (:other args)) (show-error "No input file specified.")
      (< 1 (count (:other args))) (show-error "More than one input file specified.")
      (:help args) (show-help)
      (:version args) (show-version)
      (:ir args) (c/compile-to-ir (first (:other args)) (:ir args))
      (empty? (:output args)) (show-error "No output file specified.")
      :else (c/compile-to-verilog (first (:other args)) (:output args)))))
