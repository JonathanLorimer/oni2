/*
 * ExtensionClientStoreConnector.re
 *
 * This connects the extension client to the store:
 * - Converts extension host notifications into ACTIONS
 * - Calls appropriate APIs on extension host based on ACTIONS
 */

open Oni_Core;
open Oni_Model;

module Log = (val Log.withNamespace("Oni2.Extension.ClientStoreConnector"));

module Diagnostic = Feature_LanguageSupport.Diagnostic;
module LanguageFeatures = Feature_LanguageSupport.LanguageFeatures;

let start = (extensions, extHostClient: Exthost.Client.t) => {
  let discoveredExtensionsEffect = extensions =>
    Isolinear.Effect.createWithDispatch(
      ~name="exthost.discoverExtensions", dispatch =>
      dispatch(
        Actions.Extensions(Feature_Extensions.Msg.discovered(extensions)),
      )
    );

  let registerQuitCleanupEffect =
    Isolinear.Effect.createWithDispatch(
      ~name="exthost.registerQuitCleanup", dispatch =>
      dispatch(
        Actions.RegisterQuitCleanup(
          () => Exthost.Client.terminate(extHostClient),
        ),
      )
    );

  let changeWorkspaceEffect = path =>
    Isolinear.Effect.create(~name="exthost.changeWorkspace", () => {
      Exthost.Request.Workspace.acceptWorkspaceData(
        ~workspace=Some(Exthost.WorkspaceData.fromPath(path)),
        extHostClient,
      )
    });

  let provideDecorationsEffect = {
    open Exthost.Request.Decorations;
    let nextRequestId = ref(0);

    (handle, uri) =>
      Isolinear.Effect.createWithDispatch(
        ~name="exthost.provideDecorations", dispatch => {
        let requests = [{id: nextRequestId^, handle, uri}];
        incr(nextRequestId);

        let promise =
          Exthost.Request.Decorations.provideDecorations(
            ~requests,
            extHostClient,
          );

        let toCoreDecoration:
          Exthost.Request.Decorations.decoration => Oni_Core.Decoration.t =
          decoration => {
            handle,
            tooltip: decoration.title,
            letter: decoration.letter,
            color: decoration.color.id,
          };

        Lwt.on_success(
          promise,
          decorations => {
            let decorations =
              decorations
              |> IntMap.bindings
              |> List.to_seq
              |> Seq.map(snd)
              |> Seq.map(toCoreDecoration)
              |> List.of_seq;

            dispatch(Actions.GotDecorations({handle, uri, decorations}));
          },
        );
      });
  };

  let updater = (state: State.t, action: Actions.t) =>
    switch (action) {
    | Init => (
        state,
        Isolinear.Effect.batch([
          registerQuitCleanupEffect,
          discoveredExtensionsEffect(extensions),
        ]),
      )

    | Buffers(
        Feature_Buffers.Update({update, newBuffer, triggerKey, oldBuffer}),
      ) => (
        state,
        Service_Exthost.Effects.Documents.modelChanged(
          ~previousBuffer=oldBuffer,
          ~buffer=newBuffer,
          ~update,
          extHostClient,
          () =>
          Actions.ExtensionBufferUpdateQueued({triggerKey: triggerKey})
        ),
      )

    | Buffers(Feature_Buffers.Saved(bufferId)) =>
      let effect =
        state.buffers
        |> Feature_Buffers.get(bufferId)
        |> Option.map(buffer => {
             Service_Exthost.Effects.FileSystemEventService.onFileEvent(
               ~events=
                 Exthost.Files.FileSystemEvents.{
                   created: [],
                   deleted: [],
                   changed: [buffer |> Oni_Core.Buffer.getUri],
                 },
               extHostClient,
             )
           })
        |> Option.value(~default=Isolinear.Effect.none);

      (state, effect);

    | StatusBar(ContributedItemClicked({command, _})) => (
        state,
        Service_Exthost.Effects.Commands.executeContributedCommand(
          ~command,
          ~arguments=[],
          extHostClient,
        ),
      )

    | DirectoryChanged(path) => (state, changeWorkspaceEffect(path))

    | NewDecorationProvider({handle, label}) => (
        {
          ...state,
          decorationProviders: [
            DecorationProvider.{handle, label},
            ...state.decorationProviders,
          ],
        },
        Isolinear.Effect.none,
      )

    | LostDecorationProvider({handle}) => (
        {
          ...state,
          decorationProviders:
            List.filter(
              (it: DecorationProvider.t) => it.handle != handle,
              state.decorationProviders,
            ),
        },
        Isolinear.Effect.none,
      )

    | DecorationsChanged({handle, uris}) => (
        state,
        Isolinear.Effect.batch(
          uris |> List.map(provideDecorationsEffect(handle)),
        ),
      )

    | GotDecorations({handle, uri, decorations}) => (
        {
          ...state,
          fileExplorer: {
            ...state.fileExplorer,
            decorations:
              StringMap.update(
                Uri.toFileSystemPath(uri),
                fun
                | Some(existing) => {
                    let existing =
                      List.filter(
                        (it: Decoration.t) => it.handle != handle,
                        existing,
                      );
                    Some(decorations @ existing);
                  }
                | None => Some(decorations),
                state.fileExplorer.decorations,
              ),
          },
        },
        Isolinear.Effect.none,
      )

    | _ => (state, Isolinear.Effect.none)
    };

  updater;
};
