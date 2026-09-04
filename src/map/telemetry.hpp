// telemetry.hpp
// ---------------------------------------------------------------------------
// Fase 1 (tmwa/servidor): telemetria leve via UDP, nao-bloqueante.
//
// NAO usa a maquinaria de rede/socket do proprio tmwa (Session/SessionData) --
// de proposito. E um socket UDP comum, plain POSIX, aberto uma vez e
// reaproveitado. UDP local e fire-and-forget: se a bridge (processo Python
// que escuta essa porta e reenvia por gRPC pro TelemetryService) estiver
// fora do ar, send() simplesmente falha silenciosamente e o jogo continua
// normal -- ZERO risco de travar o map-server por causa de telemetria.
//
// Ver /areas telemetry.proto e telemetry_service.py (Fase 2, ja prontos).

#pragma once

#include "fwd.hpp"

namespace tmwa
{

// chamar uma vez, em do_init_pc() (ou onde preferir no boot do map-server)
void telemetry_init();

// eventos pontuais (morte, dano, login etc). event_type e uma string curta
// tipo "player_death"/"player_hurt" (mesma convencao do TelemetryLogger.cs
// e do TelemetryEvent.type do proto). extra_int e um numero de contexto
// livre (ex: quantidade de dano) -- 0 quando nao se aplica.
void telemetry_log_event(dumb_ptr<map_session_data> sd, const char *event_type, int extra_int);

// registra o timer periodico de snapshot (chamar uma vez, junto do
// telemetry_init(), em do_init_pc()). Nao precisa mexer em mais nada --
// o timer se re-agenda sozinho, igual ao pc_natural_heal.
void telemetry_start_snapshot_timer();

} // namespace tmwa
