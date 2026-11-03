#include "ui/app.h"

int main(int argc, char** argv) {
    // hbloader passes the .nro path as argv[0] ("sdmc:/switch/pokedex.nro").
    std::string nroPath = argc > 0 && argv[0] && argv[0][0] ? argv[0] : "sdmc:/switch/pokedex.nro";
    ui::App app(nroPath);
    return app.run();
}
