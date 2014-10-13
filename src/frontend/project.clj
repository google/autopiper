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

(defproject autopiper-frontend "0.1"
  :description "Autopiper frontend"
  :main fe.main
  :source-paths ["."]
  :uberjar-name "frontend.jar"
  :url "https://github.com/google/autopiper"
  :dependencies [[org.clojure/clojure "1.5.1"]])
