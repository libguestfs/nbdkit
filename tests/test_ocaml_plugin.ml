(* nbdkit
 * Copyright (C) 2013-2021 Red Hat Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Red Hat nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *)

let sector_size = 512
let nr_sectors = 2048

let disk = Bytes.make (nr_sectors*sector_size) '\000' (* disk image *)
let sparse = Bytes.make nr_sectors '\000' (* sparseness bitmap *)

(* Test parse_* functions. *)
let () =
  assert (NBDKit.parse_size "1M" = Int64.of_int (1024*1024));
  assert (NBDKit.parse_bool "true" = true);
  assert (NBDKit.parse_bool "0" = false)

(* Test the realpath function. *)
let () =
  let isdir d = try Sys.is_directory d with Sys_error _ -> false in
  let test_dir = "/usr/bin" in
  if isdir test_dir then
    (* We don't know what the answer will be, but it must surely
     * be a directory.
     *)
    assert (isdir (NBDKit.realpath test_dir))

let load () =
  NBDKit.debug "test ocaml plugin loaded"

let unload () =
  (* A good way to find memory bugs: *)
  Gc.compact ();
  NBDKit.debug "test ocaml plugin unloaded"

let params = ref []

let config k v =
  params := (k, v) :: !params

let config_complete () =
  let params = List.rev !params in
  assert (params = [ "a", "1"; "b", "2"; "c", "3" ])

let get_ready () =
  (* We could allocate the disk here, but it's easier to allocate
   * it statically above.
   *)
  NBDKit.debug "test ocaml plugin getting ready"

let after_fork () =
  NBDKit.debug "test ocaml plugin after fork"

let cleanup () =
  NBDKit.debug "test ocaml plugin cleaning up"

(* Test the handle is received by callbacks. *)
type handle = {
  h_id : int;
  h_sentinel : string;
}

let id = ref 0
let open_connection readonly =
  let export_name = NBDKit.export_name () in
  NBDKit.debug "test ocaml plugin handle opened readonly=%b export=%S"
    readonly export_name;
  incr id;
  { h_id = !id; h_sentinel = "TESTING" }

let close h =
  NBDKit.debug "test ocaml plugin closing handle id=%d" h.h_id;
  assert (h.h_id > 0);
  assert (h.h_sentinel = "TESTING");
  ()

let list_exports _ _ =
  [ { NBDKit.name = "name1"; description = Some "desc1" };
    { name = "name2"; description = None } ]

let default_export _ _ = "name1"

let get_size h =
  NBDKit.debug "test ocaml plugin get_size handle id=%d" h.h_id;
  assert (h.h_id > 0);
  assert (h.h_sentinel = "TESTING");
  Int64.of_int (Bytes.length disk)

let pread h count offset _ =
  assert (h.h_id > 0);
  assert (h.h_sentinel = "TESTING");
  let count = Int32.to_int count in
  let buf = Bytes.create count in
  Bytes.blit disk (Int64.to_int offset) buf 0 count;
  Bytes.unsafe_to_string buf

let set_non_sparse offset len =
  Bytes.fill sparse (offset/sector_size) ((len-1)/sector_size) '\001'

let pwrite h buf offset _ =
  assert (h.h_id > 0);
  assert (h.h_sentinel = "TESTING");
  let len = String.length buf in
  let offset = Int64.to_int offset in
  String.blit buf 0 disk offset len;
  set_non_sparse offset len

let extents _ count offset _ =
  let extents = Array.init nr_sectors (
    fun sector ->
      { NBDKit.offset = Int64.of_int (sector*sector_size);
        length = Int64.of_int sector_size;
        is_hole = true; is_zero = false }
  ) in
  Bytes.iteri (
    fun i c ->
      if c = '\001' then (* not sparse *)
        extents.(i) <- { extents.(i) with is_hole = false }
  ) sparse;
  Array.to_list extents

let thread_model () =
  NBDKit.THREAD_MODEL_SERIALIZE_ALL_REQUESTS

let () =
  NBDKit.register_plugin
    ~name:   "testocaml"
    ~version: (NBDKit.version ())

    ~load
    ~get_ready
    ~after_fork
    ~cleanup
    ~unload

    ~config
    ~config_complete
    ~thread_model

    ~open_connection
    ~close
    ~get_size
    ~pread
    ~pwrite
    ~extents

    ~list_exports
    ~default_export
    ()
