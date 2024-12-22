find . -name '__pycache__' -exec rm -rf {} +
find . -name '.DS_Store' -exec rm -f {} +
find . -name '*.moc' -exec rm -f {} +
find . -name '*.o' -exec rm -f {} +
rm -rf cereal/gen
rm -rf .venv
rm -rf .mypy_cache
rm -rf panda/board/jungle/obj
rm -f panda/board/obj/*.h
rm -f panda/board/obj/version
rm -f selfdrive/controls/lib/longitudinal_mpc_lib/*.json
rm -f selfdrive/controls/lib/lateral_mpc_lib/*.json
