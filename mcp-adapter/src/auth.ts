export function extractBearerToken(authorizationHeader: string | string[] | undefined): string {
  if (Array.isArray(authorizationHeader)) {
    return extractBearerToken(authorizationHeader.at(0));
  }

  if (!authorizationHeader) {
    return '';
  }

  const [scheme, ...rest] = authorizationHeader.trim().split(/\s+/);
  if (scheme?.toLowerCase() !== 'bearer' || rest.length === 0) {
    return '';
  }

  return rest.join(' ').trim();
}

export function isAuthorized(
  authorizationHeader: string | string[] | undefined,
  apiKeys: readonly string[]
): boolean {
  if (apiKeys.length === 0) {
    return false;
  }

  const token = extractBearerToken(authorizationHeader);
  return token.length > 0 && apiKeys.includes(token);
}
