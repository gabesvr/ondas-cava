#!/usr/bin/env bash
set -e

echo "🔨 Compilando Ondas Audio Visualizer (main.cpp)..."

MOC_BIN="/usr/lib/qt6/moc"
if [ ! -f "$MOC_BIN" ]; then
    MOC_BIN=$(which moc-qt6 || which moc)
fi

echo "⚙️  Executando Qt6 Meta-Object Compiler ($MOC_BIN)..."
"$MOC_BIN" main.cpp -o main.moc

echo "📦 Compilando C++17..."
g++ -std=c++17 -fPIC main.cpp $(pkg-config --cflags --libs Qt6Widgets Qt6Core Qt6Gui) -o main

echo "✅ Compilado com sucesso!"
echo "🚀 Executando no terminal..."
exec ./main "$@"
