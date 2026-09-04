// telemetry.cpp
// ---------------------------------------------------------------------------
// Ver telemetry.hpp para o design geral. Resumo: socket UDP comum (POSIX
// puro, nao usa a infra de rede do proprio tmwa), aberto uma vez, sempre
// nao-bloqueante. Cada evento/snapshot vira UMA linha JSON por datagrama --
// sem esperar resposta, sem fila, sem retry (isso fica todo do lado da
// "bridge" em Python, que escuta essa porta e reenvia por gRPC).
//
// AVISO HONESTO: eu li pc.cpp/map.cpp/map.hpp/timer.hpp/timer.t.hpp/ids.hpp/
// wrap.hpp diretamente pra confirmar os campos e assinaturas usados aqui,
// mas NAO consegui compilar isto contra o build de verdade do tmwa (nao
// tenho o toolchain/dependencias completas no sandbox). Ao contrario do
// TelemetryService em Python (que rodei de ponta a ponta com Postgres real),
// este arquivo e o que tem MAIS chance de precisar de um ajuste pequeno na
// primeira compilada -- me manda o erro que eu corrijo rapido.

#include "telemetry.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../net/timer.hpp"
#include "map.hpp"
#include "pc.hpp"

namespace tmwa
{

// ajuste aqui se a bridge Python escutar em outro host/porta
static const char *TELEMETRY_HOST = "127.0.0.1";
static const int TELEMETRY_PORT = 9999;

// a cada quanto tempo manda um snapshot completo de cada jogador online
static const interval_t SNAPSHOT_INTERVAL = std::chrono::milliseconds(500);

static int telemetry_fd = -1;

void telemetry_init()
{
    telemetry_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (telemetry_fd < 0)
    {
        PRINTF("[telemetry] falha ao criar socket UDP: %s\n"_fmt, strerror(errno));
        return;
    }

    // nao-bloqueante: mesmo que o buffer de envio encha (bridge fora do ar,
    // por exemplo), send() nunca trava o map-server -- so falha e seguimos.
    int flags = fcntl(telemetry_fd, F_GETFL, 0);
    fcntl(telemetry_fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TELEMETRY_PORT);
    if (inet_pton(AF_INET, TELEMETRY_HOST, &addr.sin_addr) != 1)
    {
        PRINTF("[telemetry] endereco invalido: %s\n"_fmt, TELEMETRY_HOST);
        close(telemetry_fd);
        telemetry_fd = -1;
        return;
    }

    // connect() num socket UDP so define o destino padrao pra send() --
    // continua sem handshake, sem estado de conexao de verdade.
    if (::connect(telemetry_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        PRINTF("[telemetry] falha ao conectar socket UDP: %s\n"_fmt, strerror(errno));
        close(telemetry_fd);
        telemetry_fd = -1;
        return;
    }

    PRINTF("[telemetry] UDP pronto, mandando pra %s:%d\n"_fmt, TELEMETRY_HOST, TELEMETRY_PORT);
}

static void telemetry_send_line(const std::string& line)
{
    if (telemetry_fd < 0)
        return;
    // fire-and-forget de proposito: nao checa erro de EAGAIN/EWOULDBLOCK,
    // um datagrama perdido ocasionalmente e aceitavel pra telemetria.
    ::send(telemetry_fd, line.data(), line.size(), 0);
}

// serializa os campos de um player numa linha JSON. Fica tudo nesta UNICA
// funcao de proposito -- se algum campo/tipo mudar de nome no resto do
// codigo, so precisa ajustar aqui.
static std::string player_to_json(dumb_ptr<map_session_data> sd, const char *event_type, int extra_int)
{
    char buf[512];
    // OBS: sd->status_key.char_id e um CharId (strong-typedef sobre
    // uint32_t via Wrapped<uint32_t>); ._value acessa o uint32_t cru.
    // Confirmar se compila assim -- se o campo for privado na sua versao,
    // troca por um getter equivalente que exista em ids.hpp.
    std::snprintf(buf, sizeof(buf),
        "{\"event\":\"%s\",\"char_id\":%u,\"x\":%d,\"y\":%d,"
        "\"hp\":%d,\"max_hp\":%d,\"dead\":%s,\"extra\":%d}",
        event_type,
        static_cast<unsigned>(sd->status_key.char_id._value),
        static_cast<int>(sd->bl_x),
        static_cast<int>(sd->bl_y),
        static_cast<int>(sd->status.hp),
        static_cast<int>(sd->status.max_hp),
        pc_isdead(sd) ? "true" : "false",
        extra_int);
    return std::string(buf);
}

void telemetry_log_event(dumb_ptr<map_session_data> sd, const char *event_type, int extra_int)
{
    if (!sd)
        return;
    telemetry_send_line(player_to_json(sd, event_type, extra_int));
}

static void telemetry_snapshot_tick(TimerData *, tick_t)
{
    if (telemetry_fd < 0)
        return;
    // mesmo padrao de iteracao usado em atcommand.cpp (@who, @warpto etc)
    for (dumb_ptr<map_session_data> sd = map_get_first_session();
         sd;
         sd = map_get_next_session(sd))
    {
        telemetry_send_line(player_to_json(sd, "snapshot", 0));
    }
}

void telemetry_start_snapshot_timer()
{
    Timer(gettick() + SNAPSHOT_INTERVAL, telemetry_snapshot_tick, SNAPSHOT_INTERVAL).detach();
}

} // namespace tmwa
