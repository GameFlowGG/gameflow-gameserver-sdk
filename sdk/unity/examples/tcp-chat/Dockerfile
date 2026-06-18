# Packages a Unity Linux Dedicated Server build of GameFlowTcpServer for GameFlow.
#
# First build the server in Unity: switch the platform to Dedicated Server / Linux (IL2CPP),
# build the server scene, and drop the output next to this Dockerfile as ./Build/
# (so ./Build/GameFlowServer.x86_64, ./Build/UnityPlayer.so, ./Build/GameFlowServer_Data/, …).
FROM ubuntu:22.04

RUN useradd -u 1000 -m gameserver
WORKDIR /app
COPY Build/ ./
RUN chmod +x ./GameFlowServer.x86_64

USER 1000

# GameFlow injects GAMEFLOW_DEFAULT_PORT; the server reads it (local fallback is 7777).
EXPOSE 7777

ENTRYPOINT ["./GameFlowServer.x86_64", "-batchmode", "-nographics", "-logFile", "/dev/stdout"]
