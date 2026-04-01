openapi: 3.1.0
info:
  title: QodeLoc HTTP API
  version: 0.2.3-dev
  description: HTTP API for the QodeLoc core engine.
servers:
  - url: http://127.0.0.1:3100
paths:
  /search:
    post:
      summary: Semantic search by free-form query
      operationId: searchCodebase
      requestBody:
        required: true
        content:
          application/json:
            schema:
              $ref: "#/components/schemas/SearchRequest"
      responses:
        "200":
          description: Ranked modules and symbols for the query
          content:
            application/json:
              schema:
                $ref: "#/components/schemas/SearchResponse"
        "400":
          $ref: "#/components/responses/ErrorResponse"
        "503":
          $ref: "#/components/responses/ErrorResponse"
  /explain:
    post:
      summary: Explain a symbol by name
      operationId: explainSymbol
      requestBody:
        required: true
        content:
          application/json:
            schema:
              $ref: "#/components/schemas/ExplainRequest"
      responses:
        "200":
          description: Retrieval context, rendered prompt, and LLM completion
          content:
            application/json:
              schema:
                $ref: "#/components/schemas/ExplainResponse"
        "400":
          $ref: "#/components/responses/ErrorResponse"
        "404":
          $ref: "#/components/responses/ErrorResponse"
        "503":
          $ref: "#/components/responses/ErrorResponse"
  /deps:
    post:
      summary: Inspect incoming and outgoing dependencies for a symbol
      operationId: getDependencies
      requestBody:
        required: true
        content:
          application/json:
            schema:
              $ref: "#/components/schemas/SymbolRequest"
      responses:
        "200":
          description: Direct callers, callees, and module dependencies
          content:
            application/json:
              schema:
                $ref: "#/components/schemas/DepsResponse"
        "400":
          $ref: "#/components/responses/ErrorResponse"
        "404":
          $ref: "#/components/responses/ErrorResponse"
        "503":
          $ref: "#/components/responses/ErrorResponse"
  /callers:
    post:
      summary: List direct callers for a symbol
      operationId: getCallers
      requestBody:
        required: true
        content:
          application/json:
            schema:
              $ref: "#/components/schemas/SymbolRequest"
      responses:
        "200":
          description: Direct callers of the symbol
          content:
            application/json:
              schema:
                $ref: "#/components/schemas/CallersResponse"
        "400":
          $ref: "#/components/responses/ErrorResponse"
        "404":
          $ref: "#/components/responses/ErrorResponse"
        "503":
          $ref: "#/components/responses/ErrorResponse"
  /module:
    post:
      summary: Summarize a module and its symbols
      operationId: getModuleOverview
      requestBody:
        required: true
        content:
          application/json:
            schema:
              $ref: "#/components/schemas/ModuleRequest"
      responses:
        "200":
          description: Module summary, module dependencies, and module symbols
          content:
            application/json:
              schema:
                $ref: "#/components/schemas/ModuleResponse"
        "400":
          $ref: "#/components/responses/ErrorResponse"
        "404":
          $ref: "#/components/responses/ErrorResponse"
        "503":
          $ref: "#/components/responses/ErrorResponse"
  /status:
    get:
      summary: Return service and indexing status
      operationId: getStatus
      responses:
        "200":
          description: Server status and indexing snapshot
          content:
            application/json:
              schema:
                $ref: "#/components/schemas/StatusResponse"
  /reindex:
    post:
      summary: Run an incremental reindex
      operationId: reindex
      requestBody:
        required: true
        content:
          application/json:
            schema:
              $ref: "#/components/schemas/ReindexRequest"
      responses:
        "200":
          description: Reindex result and refreshed status snapshot
          content:
            application/json:
              schema:
                $ref: "#/components/schemas/ReindexResponse"
        "400":
          $ref: "#/components/responses/ErrorResponse"
        "503":
          $ref: "#/components/responses/ErrorResponse"
components:
  responses:
    ErrorResponse:
      description: Error response
      content:
        application/json:
          schema:
            $ref: "#/components/schemas/ErrorResponse"
  schemas:
    ErrorResponse:
      type: object
      additionalProperties: false
      required:
        - error
      properties:
        error:
          type: string
    SearchRequest:
      type: object
      additionalProperties: true
      required:
        - query
      properties:
        query:
          type: string
          minLength: 1
    ExplainRequest:
      type: object
      additionalProperties: true
      properties:
        name:
          type: string
        symbol_name:
          type: string
        local_files:
          type: array
          items:
            $ref: "#/components/schemas/LocalFile"
      oneOf:
        - required: [name]
        - required: [symbol_name]
    SymbolRequest:
      type: object
      additionalProperties: true
      properties:
        name:
          type: string
        symbol_name:
          type: string
      oneOf:
        - required: [name]
        - required: [symbol_name]
    ModuleRequest:
      type: object
      additionalProperties: true
      properties:
        module_name:
          type: string
        name:
          type: string
        depth:
          type: integer
          minimum: 0
      oneOf:
        - required: [module_name]
        - required: [name]
    ReindexRequest:
      type: object
      additionalProperties: true
      properties:
        changed_files:
          type: array
          items:
            type: string
        base_ref:
          type: string
    LocalFile:
      type: object
      additionalProperties: true
      required:
        - path
        - content
      properties:
        path:
          type: string
        content:
          type: string
    SearchResponse:
      type: object
      additionalProperties: true
      required:
        - query
        - modules
        - symbols
      properties:
        query:
          type: string
        modules:
          type: array
          items:
            $ref: "#/components/schemas/ModuleHit"
        symbols:
          type: array
          items:
            $ref: "#/components/schemas/SymbolContext"
    ExplainResponse:
      type: object
      additionalProperties: true
      required:
        - name
        - symbol
        - retrieval
        - prompt
        - completion
      properties:
        name:
          type: string
        symbol:
          $ref: "#/components/schemas/IndexedSymbol"
        retrieval:
          type: object
          additionalProperties: true
          required:
            - modules
            - symbols
          properties:
            modules:
              type: array
              items:
                $ref: "#/components/schemas/ModuleHit"
            symbols:
              type: array
              items:
                $ref: "#/components/schemas/SymbolContext"
        prompt:
          $ref: "#/components/schemas/RenderedPrompt"
        completion:
          $ref: "#/components/schemas/ChatResponse"
    DepsResponse:
      type: object
      additionalProperties: true
      required:
        - name
        - symbol
        - callers
        - callees
        - module_dependencies
      properties:
        name:
          type: string
        symbol:
          $ref: "#/components/schemas/IndexedSymbol"
        callers:
          type: array
          items:
            $ref: "#/components/schemas/StoredSymbol"
        callees:
          type: array
          items:
            $ref: "#/components/schemas/StoredSymbol"
        module_dependencies:
          type: array
          items:
            $ref: "#/components/schemas/ModuleDependency"
    CallersResponse:
      type: object
      additionalProperties: true
      required:
        - name
        - symbol
        - callers
      properties:
        name:
          type: string
        symbol:
          $ref: "#/components/schemas/IndexedSymbol"
        callers:
          type: array
          items:
            $ref: "#/components/schemas/StoredSymbol"
    ModuleResponse:
      type: object
      additionalProperties: true
      required:
        - module_name
        - symbols
        - dependencies
      properties:
        module_name:
          type: string
        module:
          $ref: "#/components/schemas/ModuleRecord"
        symbols:
          type: array
          items:
            $ref: "#/components/schemas/IndexedSymbol"
        dependencies:
          type: array
          items:
            $ref: "#/components/schemas/ModuleDependency"
    StatusResponse:
      type: object
      additionalProperties: true
      required:
        - running
        - host
        - port
        - root_directory
        - symbol_count
        - module_count
        - indexed_files
        - last_indexed_at_ms
        - last_operation
        - last_stats
        - retriever_ready
        - llm_ready
      properties:
        running:
          type: boolean
        host:
          type: string
        port:
          type: integer
        root_directory:
          type: string
        symbol_count:
          type: integer
        module_count:
          type: integer
        indexed_files:
          type: integer
        last_indexed_at_ms:
          type: integer
        last_operation:
          type: string
        last_stats:
          $ref: "#/components/schemas/IndexerStats"
        retriever_ready:
          type: boolean
        llm_ready:
          type: boolean
    ReindexResponse:
      type: object
      additionalProperties: true
      required:
        - mode
        - changed_files
        - stats
        - status
        - warnings
      properties:
        mode:
          type: string
        changed_files:
          type: array
          items:
            type: string
        stats:
          $ref: "#/components/schemas/IndexerStats"
        status:
          $ref: "#/components/schemas/StatusResponse"
        warnings:
          type: array
          items:
            type: string
    StoredSymbol:
      type: object
      additionalProperties: true
      required:
        - file_path
        - module_name
        - module_path
        - kind
        - qualified_name
        - signature
        - start_line
        - end_line
      properties:
        file_path:
          type: string
        module_name:
          type: string
        module_path:
          type: string
        kind:
          type: string
        qualified_name:
          type: string
        signature:
          type: string
        start_line:
          type: integer
        end_line:
          type: integer
    IndexedSymbol:
      type: object
      additionalProperties: true
      required:
        - symbol_id
        - source_text
      allOf:
        - $ref: "#/components/schemas/StoredSymbol"
        - type: object
          properties:
            symbol_id:
              type: integer
            source_text:
              type: string
    SymbolContext:
      type: object
      additionalProperties: true
      required:
        - symbol
        - score
        - context
        - token_count
        - callers
        - callees
      properties:
        symbol:
          $ref: "#/components/schemas/IndexedSymbol"
        score:
          type: number
        context:
          type: string
        token_count:
          type: integer
        callers:
          type: array
          items:
            $ref: "#/components/schemas/StoredSymbol"
        callees:
          type: array
          items:
            $ref: "#/components/schemas/StoredSymbol"
    ModuleRecord:
      type: object
      additionalProperties: true
      required:
        - module_name
        - module_path
        - summary
        - public_symbol_count
        - header_count
      properties:
        module_name:
          type: string
        module_path:
          type: string
        summary:
          type: string
        public_symbol_count:
          type: integer
        header_count:
          type: integer
    ModuleHit:
      type: object
      additionalProperties: true
      required:
        - score
      allOf:
        - $ref: "#/components/schemas/ModuleRecord"
        - type: object
          properties:
            score:
              type: number
    ModuleDependency:
      type: object
      additionalProperties: true
      required:
        - module_name
        - module_path
        - depth
      properties:
        module_name:
          type: string
        module_path:
          type: string
        depth:
          type: integer
    IndexerStats:
      type: object
      additionalProperties: true
      required:
        - files_scanned
        - files_indexed
        - symbols_indexed
        - parse_errors
        - embedding_batches
        - elapsed_ms
      properties:
        files_scanned:
          type: integer
        files_indexed:
          type: integer
        symbols_indexed:
          type: integer
        parse_errors:
          type: integer
        embedding_batches:
          type: integer
        elapsed_ms:
          type: integer
    RenderedPrompt:
      type: object
      additionalProperties: true
      required:
        - template_name
        - context_token_limit
        - token_count
        - system_text
        - user_text
        - messages
      properties:
        template_name:
          type: string
        context_token_limit:
          type: integer
        token_count:
          type: integer
        system_text:
          type: string
        user_text:
          type: string
        messages:
          type: array
          items:
            $ref: "#/components/schemas/ChatMessage"
    ChatMessage:
      type: object
      additionalProperties: true
      required:
        - role
        - content
      properties:
        role:
          type: string
        content:
          type: string
    ChatResponse:
      type: object
      additionalProperties: true
      required:
        - content
        - raw
      properties:
        content:
          type: string
        raw:
          type: object
          additionalProperties: true
