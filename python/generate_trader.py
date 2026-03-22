#!/usr/bin/env python3
"""
generate_trader.py — Auto-generates a Frankenstein Multi-Strategy trader.py
from your C++ sweep results.

Reads: ../results/optimal_routing.json
Writes: trader.py (ready to upload to prosperity.imc.com)
"""

import json
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(SCRIPT_DIR, "..", "results")
PYTHON_DIR = SCRIPT_DIR

def main():
    routing_path = os.path.join(RESULTS_DIR, "optimal_routing.json")
    
    if not os.path.exists(routing_path):
        print(f"Error: optimal_routing.json not found at {routing_path}")
        print("Run the C++ ./sweep first to generate the portfolio routing table.")
        sys.exit(1)

    with open(routing_path, "r") as f:
        routing = json.load(f)

    print(f"[generate_trader] Loading Frankenstein Portfolio...")

    # Set up the master file with global imports
    final_code = (
        "from datamodel import OrderDepth, TradingState, Order\n"
        "from typing import Dict, List, Tuple, Any\n"
        "import math\n\n"
    )

    router_init = "class Trader:\n    def __init__(self):\n        self.modules = {}\n"
    
    for product, data in routing.items():
        strat_name = data["strategy"]
        tmpl_file = data["template"]
        params = data["params"]
        
        print(f"  -> Routing {product} to {strat_name} module...")
        
        tmpl_path = os.path.join(PYTHON_DIR, "traders", tmpl_file)
        if not os.path.exists(tmpl_path):
            print(f"  [!] Warning: Template {tmpl_file} not found. Skipping.")
            continue
            
        with open(tmpl_path, "r") as f:
            code = f.read()

        # 1. Strip redundant imports so they don't clutter the master file
        code = re.sub(r'^from datamodel import.*$', '', code, flags=re.MULTILINE)
        code = re.sub(r'^from typing import.*$', '', code, flags=re.MULTILINE)
        code = re.sub(r'^import .*$', '', code, flags=re.MULTILINE)

        # 2. Inject optimal swept parameters
        for p_name, p_val in params.items():
            upper_name = p_name.upper()
            code = re.sub(rf'{upper_name}\s*=\s*[\d.]+', f'{upper_name} = {p_val}', code)

        # 3. Rename the isolated class
        class_name = f"Trader_{product}"
        code = re.sub(r'class Trader\b\s*:', f'class {class_name}:', code)

        final_code += code.strip() + "\n\n"
        
        # 4. Add module initialization to the master router
        router_init += f"        self.modules['{product}'] = {class_name}()\n"

    # 5. Build the Master Multiplexer
    master_router = router_init + """
    def run(self, state: TradingState) -> Tuple[Dict[str, List[Order]], int, str]:
        final_result = {}
        
        for product in state.order_depths:
            if product in self.modules:
                # Isolate the state so sub-traders think they are running independently
                mock_state = TradingState(
                    traderData=state.traderData,
                    timestamp=state.timestamp,
                    listings={product: state.listings[product]} if product in state.listings else {},
                    order_depths={product: state.order_depths[product]},
                    own_trades={product: state.own_trades[product]} if product in state.own_trades else {},
                    market_trades={product: state.market_trades[product]} if product in state.market_trades else {},
                    position={product: state.position.get(product, 0)},
                    observations=state.observations
                )
                
                # Execute the specialized execution unit
                res, _, _ = self.modules[product].run(mock_state)
                if product in res:
                    final_result[product] = res[product]
                    
        return final_result, 0, ""
"""
    
    final_code += master_router
    
    output_path = os.path.join(PYTHON_DIR, "trader.py")
    with open(output_path, "w") as f:
        f.write(final_code)

    print(f"\n[trader_gen] Multi-Strat trader created")
    print(f"[trader_gen] Written: {output_path}")

if __name__ == "__main__":
    main()