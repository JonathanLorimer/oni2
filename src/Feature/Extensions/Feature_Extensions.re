open Oni_Core;
open Exthost.Extension;

include Model;

module Msg = {
  let exthost = msg => Exthost(msg);
  let storage = (~resolver, msg) => Storage({resolver, msg});
  let discovered = extensions => Discovered(extensions);
  let keyPressed = key => KeyPressed(key);
  let pasted = key => Pasted(key);

  let command = (~command, ~arguments) =>
    ExecuteCommand({command, arguments});
};

let all = ({extensions, _}) => extensions;
let activatedIds = ({activatedIds, _}) => activatedIds;

// TODO: Should be stored as proper commands instead of converting every time
let commands = model => {
  model.extensions
  |> List.map((ext: Scanner.ScanResult.t) =>
       ext.manifest.contributes.commands
     )
  |> List.flatten
  |> List.map((extcmd: Contributions.Command.t) =>
       Command.{
         id: extcmd.command,
         category: extcmd.category,
         title: Some(extcmd.title |> LocalizedToken.toString),
         icon: None,
         isEnabledWhen: extcmd.condition,
         msg:
           `Arg1(
             arg =>
               ExecuteCommand({command: extcmd.command, arguments: [arg]}),
           ),
       }
     );
};

let menus = model =>
  // Combine menu items contributed to common menus from different extensions
  List.fold_left(
    (acc, extension: Scanner.ScanResult.t) =>
      List.fold_left(
        (acc, menu: Menu.Schema.definition) =>
          StringMap.add(menu.id, menu.items, acc),
        StringMap.empty,
        extension.manifest.contributes.menus,
      )
      |> StringMap.union((_, xs, ys) => Some(xs @ ys), acc),
    StringMap.empty,
    model.extensions,
  )
  |> StringMap.to_seq
  |> Seq.map(((id, items)) => Menu.Schema.{id, items})
  |> List.of_seq;

let pick = (f, {extensions, _}) => {
  extensions
  |> List.map((scanResult: Exthost.Extension.Scanner.ScanResult.t) => {
       f(scanResult.manifest)
     });
};

let themeByName = (~name, model) => {
  model
  |> pick(manifest => manifest.contributes.themes)
  |> List.flatten
  |> List.fold_left(
       (acc, curr: Contributions.Theme.t) =>
         if (curr.label == name) {
           Some(curr);
         } else {
           acc;
         },
       None,
     );
};

let themesByName = (~filter: string, model) => {
  model
  |> pick((manifest: Exthost.Extension.Manifest.t) => {
       Exthost.Extension.Contributions.(manifest.contributes.themes)
     })
  |> List.flatten
  |> List.map(({label, _}: Exthost.Extension.Contributions.Theme.t) =>
       label
     )
  |> List.filter(label => Utility.StringEx.contains(filter, label));
};

module ListView = ListView;
module DetailsView = DetailsView;

let sub = (~setup, model) => {
  let toMsg =
    fun
    | Ok(query) => SearchQueryResults(query)
    | Error(err) => SearchQueryError(err);

  switch (model.latestQuery) {
  | Some(query) when !Service_Extensions.Query.isComplete(query) =>
    Service_Extensions.Sub.search(~setup, ~query, ~toMsg)
  | Some(_)
  | None => Isolinear.Sub.none
  };
};
